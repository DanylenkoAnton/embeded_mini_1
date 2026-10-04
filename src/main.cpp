#include <Arduino.h>

enum class LedState : uint8_t {
    Off = LOW,
    On = HIGH
};

enum class SystemMode : uint8_t {
    Blinking,
    AlwaysOn,
    AlwaysOff
};

struct Config {
   static constexpr uint8_t ledPin = 13;
    static constexpr uint8_t buttonPin = 2; // Пін з підтримкою переривань на Uno/Nano
    static constexpr uint32_t blinkIntervalMs = 500;
    static constexpr uint32_t telemetryIntervalIterations = 1000;
    static constexpr uint32_t debounceDelayMs = 50; 
};

class Led {
private:
    const uint8_t pin;

public:
    explicit constexpr Led(uint8_t pinNumber) : pin(pinNumber) {}

    void init() const {
        pinMode(pin, OUTPUT);
        set(LedState::Off);
    }

    void set(LedState state) const {
        digitalWrite(pin, static_cast<uint8_t>(state));
    }
};

namespace InterruptStorage {
    volatile bool buttonPressed = false;

    void buttonISR() {
        buttonPressed = true;
    }
}

class App {
private:
    Led led;
    SystemMode currentMode;
    LedState currentLedState;
    
    uint32_t lastBlinkTime;
    uint32_t lastDebounceTime;
    uint32_t lastLoopTime;
    uint32_t loopIterationCount;
    uint64_t totalLoopDurationUs;

    void handleButton() {
        if (!InterruptStorage::buttonPressed) return;

        uint32_t currentTime = millis();
        if ((currentTime - lastDebounceTime) > Config::debounceDelayMs) {
            switch (currentMode) {
                case SystemMode::Blinking:
                    currentMode = SystemMode::AlwaysOn;
                    break;
                case SystemMode::AlwaysOn:
                    currentMode = SystemMode::AlwaysOff;
                    break;
                case SystemMode::AlwaysOff:
                    currentMode = SystemMode::Blinking;
                    break;
            }
            lastDebounceTime = currentTime;
        }
        
        InterruptStorage::buttonPressed = false;
    }

    void updateLed() {
        switch (currentMode) {
            case SystemMode::AlwaysOn:
                led.set(LedState::On);
                break;

            case SystemMode::AlwaysOff:
                led.set(LedState::Off);
                break;

            case SystemMode::Blinking:
                uint32_t currentTime = millis();
                if (currentTime - lastBlinkTime >= Config::blinkIntervalMs) {
                    lastBlinkTime = currentTime;
                    currentLedState = (currentLedState == LedState::On) ? LedState::Off : LedState::On;
                    led.set(currentLedState);
                }
                break;
        }
    }

    void calculateTelemetry(uint32_t loopDurationUs) {
        totalLoopDurationUs += loopDurationUs;
        loopIterationCount++;

        if (loopIterationCount >= Config::telemetryIntervalIterations) {
            uint32_t averageTimeNs = static_cast<uint32_t>((totalLoopDurationUs * 1000) / loopIterationCount);
            
            Serial.print(F("Avg Superloop Time: "));
            Serial.print(averageTimeNs);
            Serial.println(F(" ns"));

            loopIterationCount = 0;
            totalLoopDurationUs = 0;
        }
    }

public:
    App() : 
        led(Config::ledPin), 
        currentMode(SystemMode::Blinking), 
        currentLedState(LedState::Off),
        lastBlinkTime(0), 
        lastDebounceTime(0), 
        lastLoopTime(0),
        loopIterationCount(0), 
        totalLoopDurationUs(0) {}

    void setup() {
        Serial.begin(115200);
        led.init();

        pinMode(Config::buttonPin, INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(Config::buttonPin), InterruptStorage::buttonISR, FALLING);
        
        lastLoopTime = micros();
    }

    void loop() {
        uint32_t startMicros = micros();

        handleButton();
        updateLed();

        uint32_t endMicros = micros();
        uint32_t loopDuration = (endMicros >= startMicros) ? (endMicros - startMicros) : (0xFFFFFFFF - startMicros + endMicros);
        
        calculateTelemetry(loopDuration);
    }
};

App app;

void setup() {
    app.setup();
}

void loop() {
    app.loop();
}