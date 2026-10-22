#include "mqtt_client.h"

#include <Arduino.h>
#include <PubSubClient.h>
#include <TinyGsmClient.h>

#include "bme.h"
#include "config.h"
#include "gps.h"
#include "gsm.h"

// Lazy-init через функции-локальные статики: гарантирует, что tcpClient
// сконструируется ПОСЛЕ modem (из другого TU). Без этого — SIOF: если
// линкер инициализирует mqtt_client.cpp раньше gsm.cpp, конструктор modem
// обнуляет sockets[], затирая запись tcpClient → URC'ы MQTT не ловятся.
static TinyGsmClient& tcp() {
  static TinyGsmClient instance(modem);
  return instance;
}
static PubSubClient& mqttClient() {
  static PubSubClient instance(tcp());
  return instance;
}

bool mqttSetup() {
  // 1. Поднимаем GPRS сессию, если ещё не открыта
  if (!modem.isGprsConnected()) {
    Serial.print(F("[GPRS] Подключение к APN: ")); Serial.println(GPRS_APN);
    if (!modem.gprsConnect(GPRS_APN, GPRS_USER, GPRS_PASS)) {
      Serial.println(F("[GPRS] ❌ Не удалось подключиться к GPRS. Проверьте APN и баланс SIM."));
      return false;
    }
    Serial.print(F("[GPRS] ✅ IP: ")); Serial.println(modem.getLocalIP());
  }

  // 2. Конфигурируем MQTT-клиент (идемпотентно)
  mqttClient().setServer(MQTT_ADDRES, MQTT_TCP_PORT);
  mqttClient().setKeepAlive(MQTT_KEEP_ALIVE_S);
  mqttClient().setBufferSize(MQTT_BUFFER_SIZE);

  // 3. Подключаемся к брокеру
  if (!mqttClient().connected()) {
    Serial.print(F("[MQTT] Подключение к ")); Serial.print(MQTT_ADDRES);
    Serial.print(F(":")); Serial.println(MQTT_TCP_PORT);
    if (!mqttClient().connect(MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD)) {
      Serial.print(F("[MQTT] ❌ Ошибка подключения, state=")); Serial.println(mqttClient().state());
      // state коды: -4 timeout, -3 lost conn, -2 failed, -1 disconnected,
      //             1 wrong proto, 2 bad client id, 3 unavailable, 4 bad creds, 5 unauthorized
      return false;
    }
    Serial.println(F("[MQTT] ✅ Подключен к брокеру."));

    // Тестовое "hello"-сообщение для проверки доставки на брокер
    char hello[MQTT_HELLO_BUF_LEN];
    snprintf(hello, sizeof(hello),
             "{\"status\":\"online\",\"device\":\"%s\",\"uptime_ms\":%lu}",
             MQTT_CLIENT_ID, (unsigned long)millis());
    Serial.print(F("[MQTT] TEST -> ")); Serial.print(MQTT_TOPIC);
    Serial.print(F(" ")); Serial.println(hello);
    if (mqttClient().publish(MQTT_TOPIC, hello)) {
      Serial.println(F("[MQTT] ✅ Тестовое сообщение отправлено."));
    } else {
      Serial.print(F("[MQTT] ❌ Ошибка отправки тестового сообщения, state=")); Serial.println(mqttClient().state());
    }
  }

  return true;
}

bool publishSensorData() {
  if (!mqttSetup()) return false;

  bme.takeForcedMeasurement();
  float t = bme.readTemperature();
  float h = bme.readHumidity();
  float p = bme.readPressure() / 100.0f;

  if (isnan(t) || isnan(h) || isnan(p)) {
    Serial.println(F("[MQTT] ⚠ BME280 вернул NaN, публикация пропущена."));
    return false;
  }

  char payload[MQTT_PAYLOAD_BUF_LEN];
  if (gps.location.isValid() && gps.location.age() < GPS_PUBLISH_FIX_AGE_MS) {
    snprintf(payload, sizeof(payload),
      "{\"temp\":%.2f,\"press\":%.2f,\"hum\":%.2f,\"lat\":%.6f,\"lng\":%.6f}",
      t, p, h, gps.location.lat(), gps.location.lng());
  } else {
    snprintf(payload, sizeof(payload),
      "{\"temp\":%.2f,\"press\":%.2f,\"hum\":%.2f}",
      t, p, h);
  }

  Serial.print(F("[MQTT] -> ")); Serial.print(MQTT_TOPIC);
  Serial.print(F(" ")); Serial.println(payload);
  if (!mqttClient().publish(MQTT_TOPIC, payload)) {
    Serial.print(F("[MQTT] ❌ Ошибка публикации, state=")); Serial.println(mqttClient().state());
    return false;
  }
  return true;
}

void mqttLoop() {
  if (mqttClient().connected()) mqttClient().loop();
}
