#include "gsm.h"

#include <Arduino.h>

#include "config.h"

static HardwareSerial gsmSerial(1);  // АППАРАТНЫЙ порт Serial1
TinyGsm modem(gsmSerial);

void tryGSMInit() {
  // 1. Поднимаем UART ДО сброса — иначе теряются boot-URC модема (RDY/+CFUN/+CPIN).
  //    Их приём в живую подтверждает, что питание и линия TX_модема→RX_ESP работают.
  gsmSerial.begin(GSM_UART_BAUD, SERIAL_8N1, GSM_RX, GSM_TX);

  // 2. Аппаратный сброс SIM800L
  pinMode(GSM_RST, OUTPUT);
  digitalWrite(GSM_RST, HIGH);
  delay(GSM_RESET_PRE_MS);
  Serial.println(F("[GSM] Выполняем аппаратный сброс модема..."));
  digitalWrite(GSM_RST, LOW);
  delay(GSM_RESET_PULSE_MS);
  digitalWrite(GSM_RST, HIGH);

  // 3. Слушаем «сырой» вывод модема во время загрузки. Байт > 0 = модем жив.
  Serial.println(F("[GSM] Жду boot-сообщения модема (RAW дамп)..."));
  uint32_t t0 = millis();
  size_t rawBytes = 0;
  while (millis() - t0 < GSM_BOOT_WAIT_MS) {
    while (gsmSerial.available() > 0) {
      int b = gsmSerial.read();
      rawBytes++;
      if (b >= 32 && b < 127) Serial.write((char)b);
      else if (b == '\r') Serial.print(F("\\r"));
      else if (b == '\n') Serial.println(F("\\n"));
      else { Serial.print(F("[0x")); Serial.print(b, HEX); Serial.print(F("]")); }
    }
    delay(10);
  }
  Serial.println();
  Serial.print(F("[GSM] Байт принято за boot: ")); Serial.println(rawBytes);
  if (rawBytes == 0) {
    Serial.println(F("[GSM] ⚠ Модем молчит после сброса. Проверьте питание / TX_модема→RX_ESP / GND."));
  }

  // 4. Синхронизация скорости (Autobauding) и очистка буфера от URC
  Serial.println(F("[GSM] Синхронизация скорости (AT x N)..."));
  for (uint8_t i = 0; i < GSM_AUTOBAUD_RETRIES; i++) {
    gsmSerial.println("AT");
    delay(GSM_AUTOBAUD_DELAY_MS);
  }
  while (gsmSerial.available() > 0) { gsmSerial.read(); }
  Serial.println(F("[GSM] Буфер очищен. Передача управления TinyGSM..."));

  // 4. Запуск TinyGSM
  if (!modem.testAT(GSM_TEST_AT_TIMEOUT_MS)) {
    Serial.println(F("[GSM] ❌ Модем не отвечает на AT. Проверьте питание и TX/RX."));
    return;
  }

  // Проверяем SIM до init(), чтобы отличить проблему SIM от проблем AT-прошивки
  SimStatus preSim = modem.getSimStatus();
  Serial.print(F("[GSM] SIM status до init(): ")); Serial.println(preSim);
  // 0=ERROR, 1=READY, 2=LOCKED, 3=ANTITHEFT_LOCKED
  if (preSim == SIM_ERROR) {
    Serial.println(F("[GSM] ❌ Модем не видит SIM. Проверьте: карта вставлена, не вверх ногами, контакты чистые, держатель исправен."));
  }

  if (!modem.init()) {
    Serial.println(F("[GSM] ❌ modem.init() не удался."));
    return;
  }
  Serial.print(F("[GSM] Прошивка: ")); Serial.println(modem.getModemInfo());

  // 5. Диагностика: SIM и сигнал
  SimStatus sim = modem.getSimStatus();
  Serial.print(F("[GSM] SIM status: ")); Serial.println(sim);
  if (sim != SIM_READY) {
    Serial.println(F("[GSM] ❌ SIM не готова (нет карты / PIN заблокирован)."));
    return;
  }

  // 5. Диагностика сигнала и ожидание его появления.
  //    Сразу после reset модем ещё не прошёл RF-калибровку и CSQ=0 — ждём стабилизации
  //    до GSM_SIGNAL_WAIT_MS, пробуя раз в секунду. Без этого waitForNetwork стартует
  //    в заведомо проигрышном состоянии и ловит CREG=3 (denied) из-за отсутствия базы.
  Serial.println(F("[GSM] Жду появления сигнала..."));
  uint32_t sigDeadline = millis() + GSM_SIGNAL_WAIT_MS;
  int csq = modem.getSignalQuality();
  while (millis() < sigDeadline && (csq == GSM_CSQ_NO_SIGNAL || csq < GSM_CSQ_MIN_USABLE)) {
    delay(1000);
    csq = modem.getSignalQuality();
    Serial.print(F("[GSM] CSQ: ")); Serial.println(csq);
  }
  if (csq == GSM_CSQ_NO_SIGNAL || csq < GSM_CSQ_MIN_USABLE) {
    Serial.println(F("[GSM] ⚠ Сигнал так и не появился — продолжаем, вдруг зарегистрируется."));
  }

  // 6. Ждём регистрации в сети. Переопрашиваем CREG вручную, т.к. waitForNetwork
  //    сдаётся при CREG=3, а это часто временное состояние у lifecell/vodafone после reset.
  Serial.println(F("[GSM] Ожидание регистрации в сети..."));
  uint32_t regDeadline = millis() + GSM_NETWORK_WAIT_TIMEOUT_MS;
  SIM800RegStatus reg = modem.getRegistrationStatus();
  while (millis() < regDeadline && reg != REG_OK_HOME && reg != REG_OK_ROAMING) {
    delay(1000);
    reg = modem.getRegistrationStatus();
  }
  if (reg == REG_OK_HOME || reg == REG_OK_ROAMING) {
    Serial.print(F("[GSM] ✅ Зарегистрирован в сети. CREG=")); Serial.println(reg);
  } else {
    Serial.print(F("[GSM] ⚠ Регистрация не завершилась, CREG=")); Serial.println(reg);
    Serial.println(F("[GSM] Пробуем всё равно — gprsConnect иногда проходит с CREG=3."));
  }
}
