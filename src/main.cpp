#include <Arduino.h>

// --- Піни -------------------------------------------------------------------
const int RELAY_PIN   = 6;   // Пін керування реле (через транзисторний ключ)
const int CONTACT_PIN = 4;   // Пін зчитування стану контакту (INPUT_PULLUP)
                             // Вимагає COM реле -> GND, NO реле -> CONTACT_PIN.

// --- Параметри вимірювання --------------------------------------------------
const int           MAX_MEASUREMENTS = 10;
const unsigned long RELAY_TIMEOUT_MS = 500;  // максимальний очікуваний час спрацювання
const unsigned long RELAY_SETTLE_MS  = 700;  // пауза між вимірюваннями

// --- Внутрішній стан --------------------------------------------------------
volatile unsigned long eventTime     = 0;
volatile bool          isTriggered   = false;
volatile int           expectedLevel = LOW;  // рівень CONTACT_PIN, якого чекаємо

unsigned long turnOnTimes[MAX_MEASUREMENTS];
unsigned long turnOffTimes[MAX_MEASUREMENTS];

// Один-єдиний ISR на CHANGE, без detach/re-attach в циклі.
// Напрям фронту визначаємо програмно — приймаємо лише той рівень,
// який зараз чекаємо (expectedLevel).
void IRAM_ATTR onContactChange() {
  if (isTriggered) return;
  if (digitalRead(CONTACT_PIN) == expectedLevel) {
    eventTime   = micros();
    isTriggered = true;
  }
}

// Виконує один вимір часу спрацювання реле.
//   prepLevel     — стан RELAY_PIN до вимірювання (протилежний цільовому)
//   triggerLevel  — рівень, у який ми перемикаємо RELAY_PIN для старту вимірювання
//   expectedPinLv — очікуваний рівень на CONTACT_PIN після спрацювання
// Повертає тривалість в мікросекундах, або 0 — якщо був таймаут.
unsigned long measureRelayDelay(int prepLevel, int triggerLevel, int expectedPinLv) {
  digitalWrite(RELAY_PIN, prepLevel);
  delay(RELAY_SETTLE_MS);

  expectedLevel = expectedPinLv;
  isTriggered   = false;

  const unsigned long startTime = micros();
  digitalWrite(RELAY_PIN, triggerLevel);

  const unsigned long t0 = millis();
  while (!isTriggered && (millis() - t0 < RELAY_TIMEOUT_MS)) {
    yield();
  }

  return isTriggered ? (eventTime - startTime) : 0;
}

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  pinMode(CONTACT_PIN, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(CONTACT_PIN), onContactChange, CHANGE);

  Serial.println("=== Старт вимірювання часу спрацювання реле ===");
  Serial.flush();
}

void loop() {
  // --- ТЕСТ 1: Вимірювання часу УВІМКНЕННЯ ---
  Serial.println("\n--- 10 вимірювань УВІМКНЕННЯ ---");
  Serial.flush();
  for (int i = 0; i < MAX_MEASUREMENTS; i++) {
    turnOnTimes[i] = measureRelayDelay(LOW, HIGH, LOW);
    if (turnOnTimes[i] > 0) {
      Serial.printf("Увімкнення %2d: %lu мкс (%.2f мс)\n",
                    i + 1, turnOnTimes[i], turnOnTimes[i] / 1000.0);
    } else {
      Serial.printf("Увімкнення %2d: Помилка (Таймаут)\n", i + 1);
    }
    Serial.flush();
  }

  // --- ТЕСТ 2: Вимірювання часу ВИМКНЕННЯ ---
  Serial.println("\n--- 10 вимірювань ВИМКНЕННЯ ---");
  Serial.flush();
  for (int i = 0; i < MAX_MEASUREMENTS; i++) {
    turnOffTimes[i] = measureRelayDelay(HIGH, LOW, HIGH);
    if (turnOffTimes[i] > 0) {
      Serial.printf("Вимкнення %2d: %lu мкс (%.2f мс)\n",
                    i + 1, turnOffTimes[i], turnOffTimes[i] / 1000.0);
    } else {
      Serial.printf("Вимкнення %2d: Помилка (Таймаут)\n", i + 1);
    }
    Serial.flush();
  }

  // --- Підсумки ---
  unsigned long totalOn = 0, totalOff = 0;
  int validOnCount = 0, validOffCount = 0;
  for (int i = 0; i < MAX_MEASUREMENTS; i++) {
    if (turnOnTimes[i]  > 0) { totalOn  += turnOnTimes[i];  validOnCount++;  }
    if (turnOffTimes[i] > 0) { totalOff += turnOffTimes[i]; validOffCount++; }
  }

  Serial.println("\n================ РЕЗУЛЬТАТИ ================");
  if (validOnCount > 0) {
    const float avgOn = (float)totalOn / validOnCount;
    Serial.printf("Середній час УВІМКНЕННЯ: %.2f мкс (%.2f мс), валідних %d/%d\n",
                  avgOn, avgOn / 1000.0, validOnCount, MAX_MEASUREMENTS);
  } else {
    Serial.println("Середній час УВІМКНЕННЯ: немає валідних вимірювань");
  }
  if (validOffCount > 0) {
    const float avgOff = (float)totalOff / validOffCount;
    Serial.printf("Середній час ВИМКНЕННЯ: %.2f мкс (%.2f мс), валідних %d/%d\n",
                  avgOff, avgOff / 1000.0, validOffCount, MAX_MEASUREMENTS);
  } else {
    Serial.println("Середній час ВИМКНЕННЯ: немає валідних вимірювань");
  }
  Serial.println("============================================");
  Serial.flush();

  while (true) { delay(1000); }
}
