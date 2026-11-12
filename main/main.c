#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/pulse_cnt.h"

#define ENCODER_A_GPIO      32
#define ENCODER_B_GPIO      33
#define ENCODER_BTN_GPIO    25
#define SERVO_GPIO          18
#define BUZZER_GPIO         19

#define SERVO_MIN_ANGLE     0
#define SERVO_MAX_ANGLE     180
#define SERVO_CENTER_ANGLE  90

#define SERVO_MIN_PULSE_US  500
#define SERVO_MAX_PULSE_US  2500
#define SERVO_FREQ_HZ       50
#define SERVO_PERIOD_US     (1000000 / SERVO_FREQ_HZ)
#define SERVO_TIMER         LEDC_TIMER_0
#define SERVO_CHANNEL       LEDC_CHANNEL_0
#define SERVO_RES_BITS      LEDC_TIMER_14_BIT

#define BUZZER_TIMER        LEDC_TIMER_1
#define BUZZER_CHANNEL      LEDC_CHANNEL_1
#define BUZZER_FREQ_HZ      2000
#define BUZZER_RES_BITS     LEDC_TIMER_10_BIT
#define BUZZER_BEEP_MS      80

#define ENCODER_COUNTS_PER_DETENT  4
#define ENCODER_POLL_MS            10

#define BTN_POLL_MS         5
#define BTN_DEBOUNCE_MS     30
#define BTN_LONG_PRESS_MS   700

#define STEP_MAX_DEG        16
#define STEP_MIN_DEG        1

static const char *TAG = "servo_enc";

typedef enum {
    EVT_ENCODER_DELTA,
    EVT_BTN_SHORT,
    EVT_BTN_LONG,
} event_type_t;

typedef struct {
    event_type_t type;
    int value;
} event_t;

static QueueHandle_t evt_queue;
static pcnt_unit_handle_t pcnt_unit;

static int current_angle = SERVO_CENTER_ANGLE;
static int step_deg = STEP_MAX_DEG;

static void servo_set_angle(int angle)
{
    if (angle < SERVO_MIN_ANGLE) angle = SERVO_MIN_ANGLE;
    if (angle > SERVO_MAX_ANGLE) angle = SERVO_MAX_ANGLE;

    uint32_t pulse_us = SERVO_MIN_PULSE_US +
        (uint32_t)(SERVO_MAX_PULSE_US - SERVO_MIN_PULSE_US) * angle / SERVO_MAX_ANGLE;
    uint32_t max_duty = (1U << SERVO_RES_BITS) - 1;
    uint32_t duty = (uint64_t)pulse_us * max_duty / SERVO_PERIOD_US;

    ledc_set_duty(LEDC_LOW_SPEED_MODE, SERVO_CHANNEL, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, SERVO_CHANNEL);
}

static void servo_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = SERVO_TIMER,
        .duty_resolution = SERVO_RES_BITS,
        .freq_hz = SERVO_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    ledc_channel_config_t channel = {
        .gpio_num = SERVO_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = SERVO_CHANNEL,
        .timer_sel = SERVO_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel));

    servo_set_angle(current_angle);
}

static void buzzer_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = BUZZER_TIMER,
        .duty_resolution = BUZZER_RES_BITS,
        .freq_hz = BUZZER_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    ledc_channel_config_t channel = {
        .gpio_num = BUZZER_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = BUZZER_CHANNEL,
        .timer_sel = BUZZER_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel));
}

static void buzzer_beep(uint32_t ms)
{
    uint32_t half_duty = (1U << BUZZER_RES_BITS) / 2;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BUZZER_CHANNEL, half_duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BUZZER_CHANNEL);
    vTaskDelay(pdMS_TO_TICKS(ms));
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BUZZER_CHANNEL, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BUZZER_CHANNEL);
}

static void encoder_init(void)
{
    pcnt_unit_config_t unit_cfg = {
        .high_limit = 4096,
        .low_limit = -4096,
    };
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_cfg, &pcnt_unit));

    pcnt_glitch_filter_config_t filter = { .max_glitch_ns = 1000 };
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(pcnt_unit, &filter));

    pcnt_chan_config_t chan_a_cfg = {
        .edge_gpio_num = ENCODER_A_GPIO,
        .level_gpio_num = ENCODER_B_GPIO,
    };
    pcnt_channel_handle_t chan_a;
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_a_cfg, &chan_a));
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(chan_a,
        PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(chan_a,
        PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

    pcnt_chan_config_t chan_b_cfg = {
        .edge_gpio_num = ENCODER_B_GPIO,
        .level_gpio_num = ENCODER_A_GPIO,
    };
    pcnt_channel_handle_t chan_b;
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_b_cfg, &chan_b));
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(chan_b,
        PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(chan_b,
        PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

    ESP_ERROR_CHECK(pcnt_unit_enable(pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_start(pcnt_unit));
}

static void encoder_task(void *arg)
{
    int accumulated = 0;
    int last_count = 0;

    for (;;) {
        int count = 0;
        pcnt_unit_get_count(pcnt_unit, &count);

        accumulated += count - last_count;
        last_count = count;

        int detents = accumulated / ENCODER_COUNTS_PER_DETENT;
        if (detents != 0) {
            accumulated -= detents * ENCODER_COUNTS_PER_DETENT;
            event_t evt = { .type = EVT_ENCODER_DELTA, .value = detents };
            xQueueSend(evt_queue, &evt, 0);
        }

        vTaskDelay(pdMS_TO_TICKS(ENCODER_POLL_MS));
    }
}

static void button_task(void *arg)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << ENCODER_BTN_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));

    int stable_level = 1;
    int last_read = 1;
    int64_t last_change_ms = 0;
    int64_t press_start_ms = 0;
    bool long_fired = false;

    for (;;) {
        int level = gpio_get_level(ENCODER_BTN_GPIO);
        int64_t now_ms = esp_timer_get_time() / 1000;

        if (level != last_read) {
            last_read = level;
            last_change_ms = now_ms;
        } else if (level != stable_level && (now_ms - last_change_ms) >= BTN_DEBOUNCE_MS) {
            stable_level = level;
            if (stable_level == 0) {
                press_start_ms = now_ms;
                long_fired = false;
            } else if (!long_fired) {
                event_t evt = { .type = EVT_BTN_SHORT };
                xQueueSend(evt_queue, &evt, 0);
            }
        }

        if (stable_level == 0 && !long_fired &&
            (now_ms - press_start_ms) >= BTN_LONG_PRESS_MS) {
            long_fired = true;
            event_t evt = { .type = EVT_BTN_LONG };
            xQueueSend(evt_queue, &evt, 0);
        }

        vTaskDelay(pdMS_TO_TICKS(BTN_POLL_MS));
    }
}

static void control_task(void *arg)
{
    event_t evt;
    for (;;) {
        if (xQueueReceive(evt_queue, &evt, portMAX_DELAY) != pdTRUE) continue;

        switch (evt.type) {
        case EVT_ENCODER_DELTA: {
            int target = current_angle + evt.value * step_deg;
            int clamped = target;
            if (clamped < SERVO_MIN_ANGLE) clamped = SERVO_MIN_ANGLE;
            if (clamped > SERVO_MAX_ANGLE) clamped = SERVO_MAX_ANGLE;

            if (clamped == current_angle) {
                ESP_LOGW(TAG, "At limit %d, cannot move further", current_angle);
                buzzer_beep(BUZZER_BEEP_MS);
            } else {
                current_angle = clamped;
                servo_set_angle(current_angle);
                ESP_LOGI(TAG, "Angle: %d (step %d)", current_angle, step_deg);
            }
            break;
        }
        case EVT_BTN_SHORT:
            step_deg /= 2;
            if (step_deg < STEP_MIN_DEG) step_deg = STEP_MAX_DEG;
            ESP_LOGI(TAG, "Step size: %d deg/tick", step_deg);
            break;
        case EVT_BTN_LONG:
            current_angle = SERVO_CENTER_ANGLE;
            servo_set_angle(current_angle);
            ESP_LOGI(TAG, "Centered: %d", current_angle);
            break;
        }
    }
}

void app_main(void)
{
    evt_queue = xQueueCreate(16, sizeof(event_t));

    servo_init();
    buzzer_init();
    encoder_init();

    xTaskCreate(encoder_task, "enc", 2048, NULL, 10, NULL);
    xTaskCreate(button_task,  "btn", 2048, NULL, 10, NULL);
    xTaskCreate(control_task, "ctl", 4096, NULL, 10, NULL);

    ESP_LOGI(TAG, "Started: angle=%d step=%d", current_angle, step_deg);
}
