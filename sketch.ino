#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <DHT.h>

#define SIMULATOR 0            // 1 = Wokwi, 0 = real board

// Pins
#define DHT_PIN   25
#define SOIL_PIN  34
#define BAT_PIN   35

// Soil calibration: measure YOUR sensor (dry air, then in water)
#define SOIL_DRY_RAW  3200
#define SOIL_WET_RAW  1400

// Potentiometer simulates a Li-ion battery: 0-3.3 V on the pin maps to this range
#define BAT_MIN_V  3.0
#define BAT_MAX_V  4.2

#if SIMULATOR
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASS = "";
#else
const char* WIFI_SSID = "YOUR_2.4GHZ_WIFI";    // <-- change
const char* WIFI_PASS = "YOUR_PASSWORD";       // <-- change
#endif

// Supabase (publishable key only)
const char* SUPA_URL = "https://wubufrivwsmjgqjnavew.supabase.co/rest/v1/readings";
const char* SUPA_KEY = "sb_publishable_DUf8fAVckXvGMFwl7d5VeA_7pFBVNNn";

const unsigned long SEND_MS = 60000;   // 60 s; use 15000 for demos

DHT dht(DHT_PIN, DHT22);
unsigned long lastSend = 0;

bool connectWiFi() {
  Serial.print("Connecting to WiFi");
  WiFi.disconnect(true);       // drop any half-finished attempt
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
  if (!ok) Serial.printf("WiFi status code: %d\n", WiFi.status());  // 1 = SSID not found, 6 = wrong password
  return ok;
}

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
  http.end();
  Serial.printf("HTTP %d\n", code);       // 201 = saved
  return code >= 200 && code < 300;
}

void setup() {
  Serial.begin(115200);
  dht.begin();
  analogReadResolution(12);
  connectWiFi();
}

void loop() {
  if (lastSend != 0 && millis() - lastSend < SEND_MS) return;
  lastSend = millis();

  if (WiFi.status() != WL_CONNECTED && !connectWiFi()) return;

  Serial.println("\n===== Reading =====");

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

  // Battery (simulated by potentiometer)
  int batRaw = readAvg(BAT_PIN);
  float vpin = batRaw * 3.3 / 4095.0;
  float vbat = BAT_MIN_V + (vpin / 3.3) * (BAT_MAX_V - BAT_MIN_V);
  Serial.printf("Battery: raw %d (%.2f V)\n", batRaw, vbat);

  char sTc[12], sH[12];
  fmtFloat(sTc, sizeof(sTc), tc);
  fmtFloat(sH,  sizeof(sH),  h);

  char payload[200];
  snprintf(payload, sizeof(payload),
    "{\"air_temp\":%s,\"humidity\":%s,"
    "\"soil_raw\":%d,\"soil_pct\":%d,\"battery_v\":%.2f}",
    sTc, sH, soilRaw, soilPct, vbat);

  bool ok = postReading(payload);
  Serial.printf("[%s] %s\n", ok ? "SENT" : "FAIL", payload);
}
