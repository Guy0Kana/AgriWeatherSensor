#include <Wire.h>
#include <Adafruit_BMP085.h>
#include <RTClib.h>
#include <DHT.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#define DHT_PIN   25
#define ONEWIRE   4
#define SOIL_PIN  34
#define BAT_PIN   35
#define RAIN_PIN  27

Adafruit_BMP085 bmp;
RTC_DS1307 rtc;
DHT dht(DHT_PIN, DHT22);
OneWire oneWire(ONEWIRE);
DallasTemperature ds18(&oneWire);

bool bmpOk = false, rtcOk = false;

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

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);
  scanI2C();

  bmpOk = bmp.begin();
  rtcOk = rtc.begin();
  if (rtcOk && !rtc.isrunning()) rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));

  dht.begin();
  ds18.begin();
  pinMode(RAIN_PIN, INPUT_PULLUP);   // button pressed = LOW = "rain"
  analogReadResolution(12);

  Serial.printf("BMP180: %s | RTC: %s\n", bmpOk ? "OK" : "FAIL", rtcOk ? "OK" : "FAIL");
}

void loop() {
  Serial.println("\n===== Reading =====");

  if (rtcOk) {
    DateTime t = rtc.now();
    Serial.printf("Time: %04d-%02d-%02d %02d:%02d:%02d\n",
                  t.year(), t.month(), t.day(), t.hour(), t.minute(), t.second());
  }

  if (bmpOk) {
    Serial.printf("BMP180: %.1f C, %.1f hPa\n",
                  bmp.readTemperature(), bmp.readPressure() / 100.0);
  }

  float h = dht.readHumidity(), tc = dht.readTemperature();
  if (isnan(h) || isnan(tc)) Serial.println("DHT22: read FAILED");
  else Serial.printf("DHT22: %.1f C, %.1f %%RH\n", tc, h);

  ds18.requestTemperatures();
  float dt = ds18.getTempCByIndex(0);
  if (dt == DEVICE_DISCONNECTED_C) Serial.println("DS18B20: not found");
  else Serial.printf("DS18B20: %.2f C\n", dt);

  int soilRaw = analogRead(SOIL_PIN);
  Serial.printf("Soil: raw %d (%d %%)\n", soilRaw, map(soilRaw, 0, 4095, 0, 100));

  int batRaw = analogRead(BAT_PIN);
  Serial.printf("Battery: raw %d (%.2f V)\n", batRaw, batRaw * 3.3 / 4095.0);

  Serial.printf("Rain: %s\n", digitalRead(RAIN_PIN) == LOW ? "YES (button pressed)" : "no");

  delay(2000);
}