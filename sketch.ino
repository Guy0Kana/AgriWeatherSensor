#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Adafruit_BMP085.h>
#include <RTClib.h>
#include <DHT.h>

// Pins (v2 schematic: no DS18B20, no rain gauge)
#define DHT_PIN   25
#define SOIL_PIN  34
#define BAT_PIN   35

// Calibration
#define SOIL_DRY_RAW  3200    // ADC reading with the sensor in dry air
#define SOIL_WET_RAW  1400    // ADC reading in water
#define VBAT_DIVIDER  2.0     // two equal resistors: battery = 2 x pin voltage

// WiFi (Wokwi simulated network; change on the real board)
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASS = "";

// Supabase (use the ANON key only, never the service_role key)
const char* SUPA_URL = "https://YOUR-PROJECT.supabase.co/rest/v1/readings";
const char* SUPA_KEY = "YOUR-ANON-KEY";

const unsigned long SEND_MS = 15000;   // 15 s for demos; use 60000 or more on the real device

Adafruit_BMP085 bmp;
RTC_DS1307 rtc;                // change to RTC_DS3231 on the real board
DHT dht(DHT_PIN, DHT22);

bool bmpOk = false, rtcOk = false;
unsigned long lastSend = 0;

void scanI2C() {
  Serial.println("--- I2C scan (SDA=21, SCL=22) ---");
  int found = 0;
  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("Device at 0x%02X\n", addr);  // expect 0x68 (RTC), 0x77 (BMP180)
      found++;
    }
  }
  if (!found) Serial.println("No I2C devices found");
}

void connectWiFi() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASS, 6);   // channel 6 speeds up connection in Wokwi
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
  }
  Serial.println(" connected");
}

// NaN is invalid JSON, so send null instead
void fmtFloat(char* out, size_t n, float v) {
  if (isnan(v)) strncpy(out, "null", n);
  else snprintf(out, n, "%.1f", v);
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
  Wire.begin(21, 22);
  scanI2C();

  bmpOk = bmp.begin();
  rtcOk = rtc.begin();
  if (rtcOk && !rtc.isrunning()) rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));

  dht.begin();
  analogReadResolution(12);

  Serial.printf("BMP180: %s | RTC: %s\n", bmpOk ? "OK" : "FAIL", rtcOk ? "OK" : "FAIL");

  connectWiFi();
}

void loop() {
  if (millis() - lastSend < SEND_MS && lastSend != 0) return;
  lastSend = millis();
  if (WiFi.status() != WL_CONNECTED) connectWiFi();

  Serial.println("\n===== Reading =====");

  char ts[24] = "n/a";
  if (rtcOk) {
    DateTime t = rtc.now();
    snprintf(ts, sizeof(ts), "%04d-%02d-%02d %02d:%02d:%02d",
             t.year(), t.month(), t.day(), t.hour(), t.minute(), t.second());
    Serial.printf("Time: %s\n", ts);
  }

  float bmpT = NAN, bmpP = NAN;
  if (bmpOk) {
    bmpT = bmp.readTemperature();
    bmpP = bmp.readPressure() / 100.0;
    Serial.printf("BMP180: %.1f C, %.1f hPa\n", bmpT, bmpP);
  }

  float h = dht.readHumidity(), tc = dht.readTemperature();
  if (isnan(h) || isnan(tc)) Serial.println("DHT22: read FAILED");
  else Serial.printf("DHT22: %.1f C, %.1f %%RH\n", tc, h);

  // Soil moisture: high ADC value = dry, low = wet
  int soilRaw = analogRead(SOIL_PIN);
  int soilPct = constrain(map(soilRaw, SOIL_DRY_RAW, SOIL_WET_RAW, 0, 100), 0, 100);
  Serial.printf("Soil: raw %d (%d %% moisture)\n", soilRaw, soilPct);

  // Battery: pin sees half the battery voltage
  int batRaw = analogRead(BAT_PIN);
  float vbat = batRaw * 3.3 / 4095.0 * VBAT_DIVIDER;
  Serial.printf("Battery: raw %d (%.2f V)\n", batRaw, vbat);

  // Build JSON (keys match the Supabase column names) and send
  char sBmpT[12], sBmpP[12], sTc[12], sH[12];
  fmtFloat(sBmpT, sizeof(sBmpT), bmpT);
  fmtFloat(sBmpP, sizeof(sBmpP), bmpP);
  fmtFloat(sTc,   sizeof(sTc),   tc);
  fmtFloat(sH,    sizeof(sH),    h);

  char payload[400];
  snprintf(payload, sizeof(payload),
    "{\"device_time\":\"%s\",\"bmp_temp\":%s,\"pressure_hpa\":%s,"
    "\"air_temp\":%s,\"humidity\":%s,"
    "\"soil_raw\":%d,\"soil_pct\":%d,\"battery_v\":%.2f}",
    ts, sBmpT, sBmpP, sTc, sH, soilRaw, soilPct, vbat);

  bool ok = postReading(payload);
  Serial.printf("[%s] %s\n", ok ? "SENT" : "FAIL", payload);
}
