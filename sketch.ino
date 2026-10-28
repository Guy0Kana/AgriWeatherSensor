#include <Wire.h>
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

Adafruit_BMP085 bmp;
RTC_DS1307 rtc;                // change to RTC_DS3231 on the real board
DHT dht(DHT_PIN, DHT22);

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

  // Soil moisture: high ADC value = dry, low = wet
  int soilRaw = analogRead(SOIL_PIN);
  int soilPct = constrain(map(soilRaw, SOIL_DRY_RAW, SOIL_WET_RAW, 0, 100), 0, 100);
  Serial.printf("Soil: raw %d (%d %% moisture)\n", soilRaw, soilPct);

  // Battery: pin sees half the battery voltage
  int batRaw = analogRead(BAT_PIN);
  float vbat = batRaw * 3.3 / 4095.0 * VBAT_DIVIDER;
  Serial.printf("Battery: raw %d (%.2f V)\n", batRaw, vbat);

  delay(2000);
}