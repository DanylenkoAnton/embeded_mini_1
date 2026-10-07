#include <Arduino.h>

// Визначення пінів для світлодіодів
const int LED1_PIN = 5;
const int LED2_PIN = 6;
const int LED3_PIN = 7;

// Інтервали блимання (в мілісекундах)
const unsigned long INTERVAL_LED1 = 200;
const unsigned long INTERVAL_LED2 = 500;
const unsigned long INTERVAL_LED3 = 1000;

// Змінні для збереження часу останнього перемикання
unsigned long previousMillisLed1 = 0;
unsigned long previousMillisLed2 = 0;
unsigned long previousMillisLed3 = 0;

// Змінні для збереження поточного стану світлодіодів
bool stateLed1 = LOW;
bool stateLed2 = LOW;
bool stateLed3 = LOW;

void setup() {
  // Налаштування пінів на вихід
  pinMode(LED1_PIN, OUTPUT);
  pinMode(LED2_PIN, OUTPUT);
  pinMode(LED3_PIN, OUTPUT);
}

void loop() {
  // Отримуємо поточний час від моменту старту плати
  unsigned long currentMillis = millis();

  // Керування LED1 (200 мс)
  if (currentMillis - previousMillisLed1 >= INTERVAL_LED1) {
    previousMillisLed1 = currentMillis; // Запам'ятовуємо час
    stateLed1 = !stateLed1;             // Інвертуємо стан
    digitalWrite(LED1_PIN, stateLed1);  // Оновлюємо фізичний піновий стан
  }

  // Керування LED2 (500 мс)
  if (currentMillis - previousMillisLed2 >= INTERVAL_LED2) {
    previousMillisLed2 = currentMillis;
    stateLed2 = !stateLed2;
    digitalWrite(LED2_PIN, stateLed2);
  }

  // Керування LED3 (1000 мс)
  if (currentMillis - previousMillisLed3 >= INTERVAL_LED3) {
    previousMillisLed3 = currentMillis;
    stateLed3 = !stateLed3;
    digitalWrite(LED3_PIN, stateLed3);
  }
}