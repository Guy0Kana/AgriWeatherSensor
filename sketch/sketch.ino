#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <DHT.h>
#include <time.h>
#include "esp_system.h"
#include "mbedtls/gcm.h"
#include "mbedtls/base64.h"
#include "secrets.h"          // SECRET_WIFI_SSID, SECRET_WIFI_PASS, AES_KEY (never commit)

#define SIMULATOR 0           // 1 = Wokwi, 0 = real board
#define USE_CA_PINNING 1      // 1 = verify Supabase certificate (use this), 0 = skip checks (testing only)

// Pins
#define DHT_PIN   25
#define SOIL_PIN  34

// Soil calibration: measure YOUR sensor (dry air, then in water)
#define SOIL_DRY_RAW  3200
#define SOIL_WET_RAW  1400

// Time: Nairobi is UTC+3 with no daylight saving
#define TZ_OFFSET_SEC  (3 * 3600)
#define TZ_SUFFIX      "+03:00"

#if SIMULATOR
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASS = "";
#define DEVICE_ID "wokwi-sim"
#else
#define DEVICE_ID "farm1"
#endif

// Supabase (publishable key only)
const char* SUPA_URL = "https://wubufrivwsmjgqjnavew.supabase.co/rest/v1/readings";
const char* SUPA_KEY = "sb_publishable_DUf8fAVckXvGMFwl7d5VeA_7pFBVNNn";

// Root CA that signs Supabase's certificate. Public, so safe to commit.
const char* ROOT_CA = R"EOF(
-----BEGIN CERTIFICATE-----
MIICCTCCAY6gAwIBAgINAgPlwGjvYxqccpBQUjAKBggqhkjOPQQDAzBHMQswCQYD
VQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEUMBIG
A1UEAxMLR1RTIFJvb3QgUjQwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAwMDAw
WjBHMQswCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2Vz
IExMQzEUMBIGA1UEAxMLR1RTIFJvb3QgUjQwdjAQBgcqhkjOPQIBBgUrgQQAIgNi
AATzdHOnaItgrkO4NcWBMHtLSZ37wWHO5t5GvWvVYRg1rkDdc/eJkTBa6zzuhXyi
QHY7qca4R9gq55KRanPpsXI5nymfopjTX15YhmUPoYRlBtHci8nHc8iMai/lxKvR
HYqjQjBAMA4GA1UdDwEB/wQEAwIBhjAPBgNVHRMBAf8EBTADAQH/MB0GA1UdDgQW
BBSATNbrdP9JNqPV2Py1PsVq8JQdjDAKBggqhkjOPQQDAwNpADBmAjEA6ED/g94D
9J+uHXqnLrmvT/aDHQ4thQEd0dlq7A/Cr8deVl5c1RxYIigL9zC2L7F8AjEA8GE8
p/SgguMh1YQdc4acLa/KNJvxn7kjNuK8YAOdgLOaVsjh4rsUecrNIdSUtUlD
-----END CERTIFICATE-----
)EOF";

const unsigned long SEND_MS = 15000;

DHT dht(DHT_PIN, DHT22);
unsigned long lastSend = 0;

bool connectWiFi() {
  Serial.print("Connecting to WiFi");
  WiFi.disconnect(true);
  delay(200);
  WiFi.mode(WIFI_STA);
#if SIMULATOR
  WiFi.begin(WIFI_SSID, WIFI_PASS, 6);   // channel 6 only for Wokwi
#else
  WiFi.begin(SECRET_WIFI_SSID, SECRET_WIFI_PASS);
#endif
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(250);
    Serial.print(".");
  }
  bool ok = WiFi.status() == WL_CONNECTED;
  Serial.println(ok ? " connected" : " FAILED (will retry)");
  if (ok) Serial.printf("SSID: %s  IP: %s  RSSI: %d dBm\n",
                        WiFi.SSID().c_str(),
                        WiFi.localIP().toString().c_str(),
                        WiFi.RSSI());
  else Serial.printf("WiFi status code: %d\n", WiFi.status());  // 1 = SSID not found, 6 = wrong password
  return ok;
}

// ---------- Time ----------
bool timeIsValid() {
  return time(nullptr) > 1700000000;   // anything before Nov 2023 means "not synced"
}

bool syncTime() {
  Serial.print("Syncing time");
  configTime(TZ_OFFSET_SEC, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
  unsigned long start = millis();
  while (!timeIsValid() && millis() - start < 10000) {
    delay(250);
    Serial.print(".");
  }
  bool ok = timeIsValid();
  Serial.println(ok ? " done" : " FAILED (will retry)");
  return ok;
}

// Writes e.g. 2026-11-04T14:05:09+03:00 into out. Returns false if time isn't synced.
bool getTimestamp(char* out, size_t n) {
  if (!timeIsValid()) return false;
  struct tm t;
  if (!getLocalTime(&t, 1000)) return false;
  size_t len = strftime(out, n, "%Y-%m-%dT%H:%M:%S", &t);
  snprintf(out + len, n - len, "%s", TZ_SUFFIX);
  return true;
}
// --------------------------

// NaN is invalid JSON, so send null instead
void fmtFloat(char* out, size_t n, float v) {
  if (isnan(v)) strncpy(out, "null", n);
  else snprintf(out, n, "%.1f", v);
}

// Average several ADC samples to reduce noise
int readAvg(int pin, int samples = 10) {
  long sum = 0;
  for (int i = 0; i < samples; i++) {
    sum += analogRead(pin);
    delay(5);
  }
  return sum / samples;
}

// ---------- AES-128-GCM ----------
// Encrypts pt (n bytes). Output layout: ciphertext (n bytes) + 16-byte tag.
// A fresh random 12-byte IV is generated every call and returned in iv.
// The device ID is bound as AAD, so a message can't be passed off as another device's.
bool encryptGCM(const uint8_t* pt, size_t n, uint8_t iv[12], uint8_t* out) {
  esp_fill_random(iv, 12);
  mbedtls_gcm_context g;
  mbedtls_gcm_init(&g);
  int r = mbedtls_gcm_setkey(&g, MBEDTLS_CIPHER_ID_AES, AES_KEY, 128);
  if (r == 0) {
    r = mbedtls_gcm_crypt_and_tag(&g, MBEDTLS_GCM_ENCRYPT, n,
                                  iv, 12,
                                  (const uint8_t*)DEVICE_ID, strlen(DEVICE_ID),
                                  pt, out,
                                  16, out + n);
  }
  mbedtls_gcm_free(&g);
  return r == 0;
}

bool toBase64(const uint8_t* in, size_t n, char* out, size_t outSize) {
  size_t olen = 0;
  int r = mbedtls_base64_encode((unsigned char*)out, outSize - 1, &olen, in, n);
  if (r != 0) return false;
  out[olen] = '\0';
  return true;
}
// --------------------------------

bool postReading(const char* payload) {
  WiFiClientSecure client;
#if USE_CA_PINNING
  client.setCACert(ROOT_CA);             // verify the server really is Supabase
#else
  client.setInsecure();                  // testing only: skips certificate checks
#endif
  HTTPClient http;
  http.setTimeout(8000);
  http.begin(client, SUPA_URL);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("apikey", SUPA_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPA_KEY);
  http.addHeader("Prefer", "return=minimal");
  int code = http.POST(payload);
  String body = http.getString();
  http.end();
  Serial.printf("HTTP %d\n", code);       // 201 = saved, negative = connection/TLS error
  if (code >= 300 && body.length()) Serial.println("Server says: " + body);
  return code >= 200 && code < 300;
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  dht.begin();
  analogReadResolution(12);
  if (connectWiFi()) syncTime();
}

void loop() {
  if (lastSend != 0 && millis() - lastSend < SEND_MS) return;
  lastSend = millis();

  if (WiFi.status() != WL_CONNECTED && !connectWiFi()) return;
  if (!timeIsValid()) syncTime();

  Serial.println("\n===== Reading =====");

  // Certificate checks need a correct clock, so don't send until time is synced
#if USE_CA_PINNING
  if (!timeIsValid()) {
    Serial.println("Time not synced: skipping send (TLS needs correct time)");
    return;
  }
#endif

  // Timestamp: include the field only when the clock is synced
  char ts[32];
  char tsField[64] = "";
  if (getTimestamp(ts, sizeof(ts))) {
    snprintf(tsField, sizeof(tsField), "\"device_time\":\"%s\",", ts);
    Serial.printf("Time: %s\n", ts);
  } else {
    Serial.println("Time: not synced (device_time will be empty)");
  }

  // DHT22: up to 3 tries, since single reads sometimes fail
  float h = NAN, tc = NAN;
  for (int i = 0; i < 3 && (isnan(h) || isnan(tc)); i++) {
    h = dht.readHumidity();
    tc = dht.readTemperature();
    if (isnan(h) || isnan(tc)) delay(2000);
  }
  if (isnan(h) || isnan(tc)) Serial.println("DHT22: read FAILED");
  else Serial.printf("DHT22: %.1f C, %.1f %%RH\n", tc, h);

  // Soil moisture: high ADC = dry, low = wet
  int soilRaw = readAvg(SOIL_PIN);
  int soilPct = constrain(map(soilRaw, SOIL_DRY_RAW, SOIL_WET_RAW, 0, 100), 0, 100);
  Serial.printf("Soil: raw %d (%d %% moisture)\n", soilRaw, soilPct);

  char sTc[12], sH[12];
  fmtFloat(sTc, sizeof(sTc), tc);
  fmtFloat(sH,  sizeof(sH),  h);

  // 1) Plaintext JSON (this is what gets encrypted)
  char plain[64];
  int n = snprintf(plain, sizeof(plain),
    "{\"t\":%s,\"h\":%s,\"r\":%d,\"p\":%d}", sTc, sH, soilRaw, soilPct);
  if (n <= 0 || n >= (int)sizeof(plain)) {
    Serial.println("Plaintext too long, skipping");
    return;
  }

  // 2) Encrypt with AES-128-GCM
  uint8_t iv[12];
  uint8_t enc[sizeof(plain) + 16];
  if (!encryptGCM((const uint8_t*)plain, n, iv, enc)) {
    Serial.println("Encryption FAILED, skipping");
    return;
  }

  // 3) Base64 so the bytes travel as text inside JSON
  char b64Iv[24];
  char b64Data[128];
  if (!toBase64(iv, 12, b64Iv, sizeof(b64Iv)) ||
      !toBase64(enc, n + 16, b64Data, sizeof(b64Data))) {
    Serial.println("Base64 FAILED, skipping");
    return;
  }

  // 4) Build the POST body: only metadata is in the clear
  char payload[400];
  snprintf(payload, sizeof(payload),
    "{\"device_id\":\"%s\",%s\"iv\":\"%s\",\"data\":\"%s\"}",
    DEVICE_ID, tsField, b64Iv, b64Data);

  Serial.printf("Plain (not sent): %s\n", plain);
  bool ok = postReading(payload);
  Serial.printf("[%s] %s\n", ok ? "SENT" : "FAIL", payload);
}