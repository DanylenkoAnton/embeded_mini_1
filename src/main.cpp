#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <TinyGPS++.h>

constexpr uint8_t  SDA_PIN            = 8;
constexpr uint8_t  SCL_PIN            = 9;
constexpr uint32_t I2C_FREQ_HZ        = 400000;
constexpr uint8_t  BME_ADDR_PRIMARY   = 0x76;
constexpr uint8_t  BME_ADDR_SECONDARY = 0x77;
constexpr float    SEA_LEVEL_HPA      = 1013.25f;
constexpr uint32_t SAMPLE_PERIOD_US   = 1000000;  // 1 с
constexpr uint32_t SERIAL_BAUD        = 115200;

// Распиновка UART для модуля GPS ATGM336H
constexpr uint8_t GPS_RX = 6;  // К этому пину подключаем RX модуля GPS
constexpr uint8_t GPS_TX = 7;  // К этому пину подключаем TX модуля GPS

Adafruit_BME280 bme;
TinyGPSPlus gps;

bool sensorReady = false;

// Используем аппаратный Serial1 для работы с GPS
HardwareSerial gpsSerial(1);

// Аппаратный таймер + флаг, выставляемый из ISR
hw_timer_t *sampleTimer = nullptr;
volatile bool sampleTick = false;

// ISR должен быть максимально коротким: просто выставляем флаг.
// Работа с I2C/Serial из прерывания недопустима, фактический опрос
// датчиков выполняется в loop() при обнаружении флага.
void IRAM_ATTR onSampleTimer() {
  sampleTick = true;
}

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

void readGPS() {
// --- Данные геолокации (GPS / BDS) ---
  const uint32_t sats = gps.satellites.isValid() ? gps.satellites.value() : 0;
  const uint32_t hdop100 = gps.hdop.isValid() ? gps.hdop.value() : 9999;  // в сотых

  Serial.print(F("[GPS]    Спутники:    ")); Serial.println(sats);
  Serial.print(F("[GPS]    HDOP:        ")); Serial.println(hdop100 / 100.0f, 2);

  // Считаем фикс надёжным только при 4+ спутниках и HDOP < 5.0 со свежими данными
  const bool goodFix = (sats >= 4) && (hdop100 < 500) &&
                       gps.location.isValid() && gps.location.age() < 2000;

  if (goodFix) {
    Serial.print(F("[GPS]    Широта:      ")); Serial.println(gps.location.lat(), 6);
    Serial.print(F("[GPS]    Долгота:     ")); Serial.println(gps.location.lng(), 6);
    if (gps.altitude.isValid()) {
      Serial.print(F("[GPS]    Высота:      ")); Serial.print(gps.altitude.meters()); Serial.println(F(" м"));
    }
    // Скорость печатаем только при свежем валидном значении; шум ниже 2 км/ч считаем нулём
    if (gps.speed.isValid() && gps.speed.age() < 2000) {
      float kmph = gps.speed.kmph();
      if (kmph < 2.0f) kmph = 0.0f;
      Serial.print(F("[GPS]    Скорость:    ")); Serial.print(kmph); Serial.println(F(" км/ч"));
    }
  } else {
    Serial.println(F("[GPS]    Координаты:  Ожидание надёжной фиксации спутников..."));
  }
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

void setup() {
  Serial.begin(SERIAL_BAUD);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 2000) { delay(10); }

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(I2C_FREQ_HZ);

  // Инициализация UART для GPS (стандартная скорость ATGM336H — 9600 бод)
  gpsSerial.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);
  Serial.println(F("Интерфейс GPS запущен на пинах RX:6, TX:7. Ожидание спутников..."));

  // Таймер 0, делитель 80 => тик 1 МГц, тревога каждую секунду с автоперезагрузкой
  sampleTimer = timerBegin(0, 80, true);
  // edge=false: ESP32-C3 поддерживает только уровневое прерывание,
  // передача true вызовет лишний log_w из HAL без изменения поведения
  timerAttachInterrupt(sampleTimer, &onSampleTimer, false);
  timerAlarmWrite(sampleTimer, SAMPLE_PERIOD_US, true);
  timerAlarmEnable(sampleTimer);
}

void loop() {
  // ВАЖНО: читаем GPS в каждой итерации loop, иначе буфер UART (256 Б)
  // переполняется и TinyGPS++ не успевает собрать полные NMEA-предложения
  // Читаем данные из GPS-модуля и передаем их в парсер TinyGPS++
  while (gpsSerial.available() > 0) {
    gps.encode(gpsSerial.read());
  }

  if (!sampleTick) return;
  sampleTick = false;

  if (!sensorReady) {
    i2cScan();
    if (trySensorInit()) {
      sensorReady = true;
      Serial.println("Датчик BME280 успешно запущен!");
    } else {
      Serial.println("Не удалось найти датчик BME280, проверьте подключение (CSB -> VCC, SDO -> GND)!");
    }
    return;
  }

  readBME();
  readGPS();
  Serial.println("----------------");
}
