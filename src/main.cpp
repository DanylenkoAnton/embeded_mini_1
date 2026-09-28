#include <Arduino.h>

int PIN_OUT = 20;
int PIN_IN = 6;

const int threshold = 300;

int readStabilized(int pin)
{
  long sum = 0;
  const int samples = 16;
  for (int i = 0; i < samples; i++)
  {
    sum += analogRead(pin);
    delayMicroseconds(50);
  }
  return sum / samples;
}

void setup()
{
  Serial.begin(115200);

  pinMode(PIN_OUT, LOW);
}

void loop()
{
    Serial.println("----------------");
    int adc = readStabilized(PIN_IN);

    Serial.printf("Light level: %d ", adc);

    if (adc < threshold) {
        digitalWrite(PIN_OUT, HIGH);
        Serial.println(" | LIGHT ON");
    } else {
        digitalWrite(PIN_OUT, LOW);
        Serial.println(" | LIGHT OFF");
    }

    delay(200);
}

// const int ledPin = 5;
// const int ledcChannel1 = 0;
// const int resolution = 8;
// const int potPin = 1;
// int currentFrequency = 1000;
// int currentDuty = 0;

// int lightValue = 0;

// const int threshold = 600;
// bool isDark = false;

// int readStabilized(int pin)
// {
//   long sum = 0;
//   const int samples = 16;
//   for (int i = 0; i < samples; i++)
//   {
//     sum += analogRead(pin);
//     delayMicroseconds(50);
//   }
//   return sum / samples;
// }

// void setup()
// {
//   Serial.begin(115200);

//   ledcSetup(ledcChannel1, currentFrequency, resolution);
//   ledcAttachPin(ledPin, ledcChannel1);
//   ledcWrite(ledcChannel1, 0);
// }

// void loop()
// {
//   lightValue = analogRead(photoResistorPin);

//   Serial.printf("Light level: %d ",lightValue);
  
//   if (lightValue < threshold)
//   {
//     isDark = true;
//   }
//   else
//   {
//     isDark = false;
//   }

//   if (isDark)
//   {
//     int potValue = readStabilized(potPin);
//     Serial.printf(" | Regulation level - %d ", potValue);
//     currentDuty = map(potValue, 0, 4095, 0, 255);

//     ledcWrite(ledcChannel1, currentDuty);

//     int dutyPercent = map(currentDuty, 0, 255, 0, 100);
//     Serial.printf(" | Duty cycle: %d%%\n", dutyPercent);
//   }
//   else
//   {
//     ledcWrite(ledcChannel1, 0);
//     Serial.println(" | LED OFF");
//   }

//   delay(200);
// }