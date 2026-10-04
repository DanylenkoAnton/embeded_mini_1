#include <Arduino.h>

// Визначення пінів
const int RELAY_PIN = 12;     // Пін керування реле
const int CONTACT_PIN = 14;   // Пін зчитування стану контакту (з INPUT_PULLUP)

// Змінні для вимірювань (volatile для використання в ISR)
volatile unsigned long eventTime = 0;
volatile bool isTriggered = false;

unsigned long startTime = 0;
const int MAX_MEASUREMENTS = 10;
unsigned long turnOnTimes[MAX_MEASUREMENTS];
unsigned long turnOffTimes[MAX_MEASUREMENTS];

// Обробник апаратного переривання
void onContactChange() {
  // Фіксуємо час лише для першого коливання (ігноруємо брязкіт)
  if (!isTriggered) {
    eventTime = micros();
    isTriggered = true;
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW); // Початковий стан — вимкнено
  
  // Налаштування піна з вбудованим підтягуючим резистором
  pinMode(CONTACT_PIN, INPUT_PULLUP);
  
  Serial.println("=== Старт вимірювання часу спрацювання реле ===");
}

void loop() {
  // --- ТЕСТ 1: Вимірювання часу УВІМКНЕННЯ (Turn-On Time) ---
  Serial.println("\n--- Запуск 10 вимірювань увімкнення ---");
  for (int i = 0; i < MAX_MEASUREMENTS; i++) {
    isTriggered = false;
    // Очікуємо стабілізації перед тестом
    delay(200); 
    
    // Налаштовуємо переривання на спад (FALLING), бо COM з'єднаний з GND
    attachInterrupt(digitalPinToInterrupt(CONTACT_PIN), onContactChange, FALLING);
    
    startTime = micros();
    digitalWrite(RELAY_PIN, HIGH); // Вмикаємо реле
    
    // Очікуємо на спрацювання переривання (з таймаутом 100 мс на випадок помилки)
    unsigned long timeout = millis();
    while (!isTriggered && (millis() - timeout < 100)) {
      yield(); 
    }
    
    detachInterrupt(digitalPinToInterrupt(CONTACT_PIN));
    
    if (isTriggered) {
      turnOnTimes[i] = eventTime - startTime;
      Serial.printf("Увімкнення %d: %lu мкс (%.2f мс)\n", i + 1, turnOnTimes[i], turnOnTimes[i] / 1000.0);
    } else {
      Serial.printf("Увімкнення %d: Помилка (Таймаут)\n", i + 1);
      turnOnTimes[i] = 0;
    }
  }

  // --- ТЕСТ 2: Вимірювання часу ВИМКНЕННЯ (Turn-Off Time) ---
  Serial.println("\n--- Запуск 10 вимірювань вимкнення ---");
  for (int i = 0; i < MAX_MEASUREMENTS; i++) {
    isTriggered = false;
    delay(200); 
    
    // Налаштовуємо на зростання (RISING), бо при розмиканні вбудований pull-up підніме пін до 3.3V
    attachInterrupt(digitalPinToInterrupt(CONTACT_PIN), onContactChange, RISING);
    
    startTime = micros();
    digitalWrite(RELAY_PIN, LOW); // Вимикаємо реле
    
    unsigned long timeout = millis();
    while (!isTriggered && (millis() - timeout < 100)) {
      yield();
    }
    
    detachInterrupt(digitalPinToInterrupt(CONTACT_PIN));
    
    if (isTriggered) {
      turnOffTimes[i] = eventTime - startTime;
      Serial.printf("Вимкнення %d: %lu мкс (%.2f мс)\n", i + 1, turnOffTimes[i], turnOffTimes[i] / 1000.0);
    } else {
      Serial.printf("Вимкнення %d: Помилка (Таймаут)\n", i + 1);
      turnOffTimes[i] = 0;
    }
  }

  // --- Обчислення та виведення середнього значення ---
  unsigned long totalOn = 0;
  unsigned long totalOff = 0;
  int validOnCount = 0;
  int validOffCount = 0;

  for (int i = 0; i < MAX_MEASUREMENTS; i++) {
    if (turnOnTimes[i] > 0) { totalOn += turnOnTimes[i]; validOnCount++; }
    if (turnOffTimes[i] > 0) { totalOff += turnOffTimes[i]; validOffCount++; }
  }

  Serial.println("\n================ РЕЗУЛЬТАТИ ================");
  if (validOnCount > 0) {
    float avgOn = (float)totalOn / validOnCount;
    Serial.printf("Середній час УВІМКНЕННЯ: %.2f мкс (%.2f мс)\n", avgOn, avgOn / 1000.0);
  }
  if (validOffCount > 0) {
    float avgOff = (float)totalOff / validOffCount;
    Serial.printf("Середній час ВИМКНЕННЯ: %.2f мкс (%.2f мс)\n", avgOff, avgOff / 1000.0);
  }
  Serial.println("============================================");

  // Зупиняємо виконання, щоб результати не зациклювалися в Serial Monitor
  while (true) {
    delay(1000);
  }
}
