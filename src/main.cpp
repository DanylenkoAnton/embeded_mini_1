#include <Arduino.h>

// --- Old (interrupt + time-based debounce) — kept for reference, to be removed ---
// volatile bool BUTTON_PUSHED = false;
// unsigned long previousPushTime = 0;
//
// void IRAM_ATTR buttonISR() {
//     BUTTON_PUSHED = true;
// }
//
// void buttonStateSubscriber() {
//     if(BUTTON_PUSHED && previousPushTime + DEBOUNCE_MS < millis()) {
//         counter++;
//         previousPushTime = millis();
//         BUTTON_PUSHED = false;
//     }
// }
//
// In setup(): attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), buttonISR, FALLING);
// --------------------------------------------------------------------------------

// Task 4: Polling + debounce as a state machine (no interrupts)
// Poll the pin every POLL_MS; a 4-state FSM filters bouncing on both press and release.

unsigned counter = 0;

const unsigned int BUTTON_PIN = 1;
const unsigned long DEBOUNCE_MS = 50;
const unsigned long POLL_MS = 5;

enum class BtnState { IDLE, DEBOUNCE_PRESS, HELD, DEBOUNCE_RELEASE };
BtnState btnState = BtnState::IDLE;
unsigned long stateEnteredAt = 0;
unsigned long lastPollAt = 0;

void setup() {
    Serial.begin(115200);
    pinMode(BUTTON_PIN, INPUT_PULLUP);
}

void pollButton() {
    unsigned long now = millis();
    if (now - lastPollAt < POLL_MS) return;
    lastPollAt = now;

    bool pressed = (digitalRead(BUTTON_PIN) == LOW);

    switch (btnState) {
        case BtnState::IDLE:
            if (pressed) {
                btnState = BtnState::DEBOUNCE_PRESS;
                stateEnteredAt = now;
            }
            break;

        case BtnState::DEBOUNCE_PRESS:
            if (!pressed) {
                btnState = BtnState::IDLE;
            } else if (now - stateEnteredAt >= DEBOUNCE_MS) {
                counter++;
                btnState = BtnState::HELD;
            }
            break;

        case BtnState::HELD:
            if (!pressed) {
                btnState = BtnState::DEBOUNCE_RELEASE;
                stateEnteredAt = now;
            }
            break;

        case BtnState::DEBOUNCE_RELEASE:
            if (pressed) {
                btnState = BtnState::HELD;
            } else if (now - stateEnteredAt >= DEBOUNCE_MS) {
                btnState = BtnState::IDLE;
            }
            break;
    }
}

void loop() {
    Serial.println("-----------");
    pollButton();

    Serial.println(counter);
}

/*
    Method        | Additional counts | Delay   | Complexity | Comment
    No Debounce   | 1-4               | None.   | Easy.      | This is easy implementing but there are additional triggers
    With Debounce | interaction ended | None.   | Easy.      | Is easy implementing and more accurate than previous method
    State Based.  | 0                 | None    | Medium.    | Totally correct count but small delay exists
    Polling FSM   | 0                 | ~50ms   | Medium.    | No ISR, 4-state FSM debounces press & release, deterministic timing
    Hardware RC   | 1(0 for 3 and 4). | None.   | Hard.      | It works with perfect accuracy, but additional hardware is needed
*/
