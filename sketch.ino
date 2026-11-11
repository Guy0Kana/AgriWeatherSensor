#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <DHT.h>
#include <time.h>

#define SIMULATOR 0           // 1 = Wokwi, 0 = real board

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
const char* WIFI_SSID = "";    // <-- change
const char* WIFI_PASS = "";       // <-- change
#define DEVICE_ID "farm1"
#endif

// Supabase (publishable key only)
const char* SUPA_URL = "https://wubufrivwsmjgqjnavew.supabase.co/rest/v1/readings";
const char* SUPA_KEY = "sb_publishable_DUf8fAVckXvGMFwl7d5VeA_7pFBVNNn";

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
  WiFi.begin(WIFI_SSID, WIFI_PASS);
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

bool postReading(const char* payload) {
  WiFiClientSecure client;
  client.setInsecure();                  // prototype only: skips certificate checks
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
  Serial.printf("HTTP %d\n", code);       // 201 = saved
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

  char payload[260];
  snprintf(payload, sizeof(payload),
    "{\"device_id\":\"%s\",%s\"air_temp\":%s,\"humidity\":%s,"
    "\"soil_raw\":%d,\"soil_pct\":%d}",
    DEVICE_ID, tsField, sTc, sH, soilRaw, soilPct);

  bool ok = postReading(payload);
  Serial.printf("[%s] %s\n", ok ? "SENT" : "FAIL", payload);
}