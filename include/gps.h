#pragma once

#include <TinyGPS++.h>

extern TinyGPSPlus gps;

void tryGPSInit();
void gpsFeed();   // перегоняем байты из UART в парсер (вызывать каждый loop)
void readGPS();
