#pragma once

#include <Adafruit_BME280.h>

extern Adafruit_BME280 bme;

void i2cScan();
bool trySensorInit();
void readBME();
