#include "gps.h"

#include <Arduino.h>
#include <SoftwareSerial.h>

#include "config.h"

TinyGPSPlus gps;
static SoftwareSerial gpsSerial;

void tryGPSInit() {
  gpsSerial.begin(GPS_UART_BAUD, SWSERIAL_8N1, GPS_RX, GPS_TX, false,
                  GPS_SW_RX_BUF_BYTES, GPS_SW_ISR_BUF_BYTES);
  Serial.println(F("[GPS] Программный порт запущен на пинах 6 и 7."));
}

void gpsFeed() {
  // ВАЖНО: читаем GPS в каждой итерации loop, иначе буфер UART (256 Б)
  // переполняется и TinyGPS++ не успевает собрать полные NMEA-предложения.
  while (gpsSerial.available() > 0) {
    gps.encode(gpsSerial.read());
  }
}

void readGPS() {
  // --- Данные геолокации (GPS / BDS) ---
  const uint32_t sats = gps.satellites.isValid() ? gps.satellites.value() : 0;
  const uint32_t hdop100 = gps.hdop.isValid() ? gps.hdop.value() : GPS_HDOP_INVALID;

  Serial.print(F("[GPS]    Спутники:    ")); Serial.println(sats);
  Serial.print(F("[GPS]    HDOP:        ")); Serial.println(hdop100 / 100.0f, 2);

  const bool goodFix = (sats >= GPS_MIN_SATELLITES) &&
                       (hdop100 < GPS_MAX_HDOP_X100) &&
                       gps.location.isValid() &&
                       gps.location.age() < GPS_MAX_FIX_AGE_MS;

  if (goodFix) {
    Serial.print(F("[GPS]    Широта:      ")); Serial.println(gps.location.lat(), 6);
    Serial.print(F("[GPS]    Долгота:     ")); Serial.println(gps.location.lng(), 6);
    if (gps.altitude.isValid()) {
      Serial.print(F("[GPS]    Высота:      ")); Serial.print(gps.altitude.meters()); Serial.println(F(" м"));
    }
    if (gps.speed.isValid() && gps.speed.age() < GPS_MAX_SPEED_AGE_MS) {
      float kmph = gps.speed.kmph();
      if (kmph < GPS_SPEED_NOISE_KMPH) kmph = 0.0f;
      Serial.print(F("[GPS]    Скорость:    ")); Serial.print(kmph); Serial.println(F(" км/ч"));
    }
  } else {
    Serial.println(F("[GPS]    Координаты:  Ожидание надёжной фиксации спутников..."));
  }
}
