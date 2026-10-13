#include <Arduino.h>
#include <Wire.h>

#include "bme.h"
#include "config.h"
#include "gps.h"
#include "gsm.h"
#include "mqtt_client.h"

bool sensorReady = false;

// Аппаратный таймер + флаг, выставляемый из ISR
hw_timer_t *sampleTimer = nullptr;
volatile bool sampleTick = false;

// ISR должен быть максимально коротким: просто выставляем флаг.
// Работа с I2C/Serial из прерывания недопустима, фактический опрос
// датчиков выполняется в loop() при обнаружении флага.
void IRAM_ATTR onSampleTimer() {
  sampleTick = true;
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < SERIAL_WAIT_TIMEOUT_MS) { delay(SERIAL_WAIT_POLL_MS); }

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(I2C_FREQ_HZ);

  tryGPSInit();
  tryGSMInit();

  // Таймер 0: тик 1 МГц (см. SAMPLE_TIMER_DIVIDER), тревога раз в SAMPLE_PERIOD_US с автоперезагрузкой
  sampleTimer = timerBegin(0, SAMPLE_TIMER_DIVIDER, true);
  // edge=false: ESP32-C3 поддерживает только уровневое прерывание,
  // передача true вызовет лишний log_w из HAL без изменения поведения
  timerAttachInterrupt(sampleTimer, &onSampleTimer, false);
  timerAlarmWrite(sampleTimer, SAMPLE_PERIOD_US, true);
  timerAlarmEnable(sampleTimer);
}

void loop() {
  gpsFeed();
  mqttLoop();

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

  // Публикация в MQTT не чаще раза в минуту
  static uint32_t lastPublishMs = 0;
  const uint32_t now = millis();
  if (lastPublishMs == 0 || now - lastPublishMs >= MQTT_PUBLISH_INTERVAL_MS) {
    publishSensorData();
    lastPublishMs = now;
  }

  Serial.println("----------------");
}
