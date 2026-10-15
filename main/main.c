#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_attr.h"

#define BUTTON_PIN   GPIO_NUM_1
#define DEBOUNCE_MS  50
#define POLL_MS      5

static inline uint32_t millis(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

// --- Old (interrupt + time-based debounce) — kept for reference, to be removed ---
static volatile bool BUTTON_PUSHED = false;
static uint32_t previousPushTime = 0;
static volatile uint32_t isrMillis = 0;
static unsigned counter = 0;

static void IRAM_ATTR buttonISR(void *arg) {
    BUTTON_PUSHED = true;
    isrMillis = (uint32_t)(esp_timer_get_time() / 1000);
}

static void buttonStateSubscriber(void) {
    if (BUTTON_PUSHED && previousPushTime + DEBOUNCE_MS < millis()) {
        counter++;
        previousPushTime = millis();
        BUTTON_PUSHED = false;

        printf("%" PRIu32 "\n", millis() - isrMillis);
    }
}
// --------------------------------------------------------------------------------

// Task 4: Polling + debounce as a state machine (no interrupts)
// Poll the pin every POLL_MS; a 4-state FSM filters bouncing on both press and release.
typedef enum {
    BTN_IDLE,
    BTN_DEBOUNCE_PRESS,
    BTN_HELD,
    BTN_DEBOUNCE_RELEASE,
} BtnState;

static BtnState btnState = BTN_IDLE;
static uint32_t stateEnteredAt = 0;
static uint32_t lastPollAt = 0;

static void pollButton(void) {
    uint32_t now = millis();
    if (now - lastPollAt < POLL_MS) return;
    lastPollAt = now;

    bool pressed = (gpio_get_level(BUTTON_PIN) == 0);

    switch (btnState) {
        case BTN_IDLE:
            if (pressed) {
                btnState = BTN_DEBOUNCE_PRESS;
                stateEnteredAt = now;
            }
            break;

        case BTN_DEBOUNCE_PRESS:
            if (!pressed) {
                btnState = BTN_IDLE;
            } else if (now - stateEnteredAt >= DEBOUNCE_MS) {
                counter++;
                btnState = BTN_HELD;
            }
            break;

        case BTN_HELD:
            if (!pressed) {
                btnState = BTN_DEBOUNCE_RELEASE;
                stateEnteredAt = now;
            }
            break;

        case BTN_DEBOUNCE_RELEASE:
            if (pressed) {
                btnState = BTN_HELD;
            } else if (now - stateEnteredAt >= DEBOUNCE_MS) {
                btnState = BTN_IDLE;
            }
            break;
    }
}

void app_main(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << BUTTON_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&io_conf);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_PIN, buttonISR, NULL);

    while (1) {
        buttonStateSubscriber();
        // pollButton();

        // printf("%u\n", counter);
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

/*
    Method        | Additional counts | Delay                                | Complexity | Comment
    No Debounce   | 1-4               | 0                                    | Easy.      | This is easy implementing but there are additional triggers
    With Debounce | interaction ended | ~50ms                                | Easy.      | Is easy implementing and more accurate than previous method
    State Based.  | 0                 | 0                                    | Medium.    | Totally correct count but small delay exists
    Polling FSM   | 0                 | ~50ms                                | Medium.    | No ISR, 4-state FSM debounces press & release, deterministic timing
    Hardware RC   | 1(0 for 3 and 4). | None. (just when capacitor is empty) | Hard.      | It works with perfect accuracy, but additional hardware is needed
*/
