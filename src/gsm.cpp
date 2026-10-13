#include "gsm.h"

#include <Arduino.h>

#include "config.h"

static HardwareSerial gsmSerial(1);  // АППАРАТНЫЙ порт Serial1
TinyGsm modem(gsmSerial);

void tryGSMInit() {
  // 1. Аппаратный сброс SIM800L
  pinMode(GSM_RST, OUTPUT);
  digitalWrite(GSM_RST, HIGH);
  delay(GSM_RESET_PRE_MS);
  Serial.println(F("[GSM] Выполняем аппаратный сброс модема..."));
  digitalWrite(GSM_RST, LOW);
  delay(GSM_RESET_PULSE_MS);
  digitalWrite(GSM_RST, HIGH);

  // Даём модему время полностью загрузиться и выплюнуть в порт RDY и +CFUN
  delay(GSM_BOOT_WAIT_MS);

  // 2. Запуск аппаратного Serial1 для GSM модема
  gsmSerial.begin(GSM_UART_BAUD, SERIAL_8N1, GSM_RX, GSM_TX);

  // 3. Синхронизация скорости GSM (Autobauding) и очистка буфера
  Serial.println(F("[GSM] Синхронизация скорости..."));
  for (uint8_t i = 0; i < GSM_AUTOBAUD_RETRIES; i++) {
    gsmSerial.println("AT");
    delay(GSM_AUTOBAUD_DELAY_MS);
  }

  // Вычитываем и удаляем весь «мусор» (сообщения RDY, CPIN), скопившийся в буфере
  while (gsmSerial.available() > 0) {
    gsmSerial.read();
  }
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

  int csq = modem.getSignalQuality();
  Serial.print(F("[GSM] CSQ: ")); Serial.println(csq);
  if (csq == GSM_CSQ_NO_SIGNAL || csq < GSM_CSQ_MIN_USABLE) {
    Serial.println(F("[GSM] ⚠ Нет сигнала или слишком слабый. Проверьте антенну."));
  }

  // 6. Ждём регистрации в сети
  Serial.println(F("[GSM] Ожидание регистрации в сети..."));
  if (modem.waitForNetwork(GSM_NETWORK_WAIT_TIMEOUT_MS)) {
    Serial.println(F("[GSM] ✅ Зарегистрирован в сети."));
  } else {
    Serial.print(F("[GSM] ❌ Регистрация не удалась. CREG=")); Serial.println(modem.getRegistrationStatus());
  }
}
