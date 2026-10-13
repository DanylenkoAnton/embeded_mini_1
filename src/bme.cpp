#include "bme.h"

#include <Arduino.h>
#include <Wire.h>

#include "config.h"

Adafruit_BME280 bme;

void i2cScan() {
  Serial.printf("I2C scan on SDA=%u SCL=%u...\n", SDA_PIN, SCL_PIN);
  uint8_t found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  device at 0x%02X\n", addr);
      found++;
    }
  }
  Serial.printf("Scan done, %u device(s) found\n", found);
}

bool trySensorInit() {
  if (!bme.begin(BME_ADDR_PRIMARY, &Wire) &&
      !bme.begin(BME_ADDR_SECONDARY, &Wire)) {
    return false;
  }
  bme.setSampling(Adafruit_BME280::MODE_FORCED,
                  Adafruit_BME280::SAMPLING_X1,
                  Adafruit_BME280::SAMPLING_X1,
                  Adafruit_BME280::SAMPLING_X1,
                  Adafruit_BME280::FILTER_OFF);
  return true;
}

void readBME() {
  bme.takeForcedMeasurement();

  float t = bme.readTemperature();
  float h = bme.readHumidity();
  float p = bme.readPressure() / 100.0f;
  float a = bme.readAltitude(SEA_LEVEL_HPA);

  if (isnan(t) || isnan(h) || isnan(p)) {
    Serial.println("Ошибка чтения BME280 (NaN)");
    return;
  }

  Serial.printf("T=%.2f *C  H=%.2f %%  P=%.2f hPa  Alt=%.2f m\n", t, h, p, a);
}
