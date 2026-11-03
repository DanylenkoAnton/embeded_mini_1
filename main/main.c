#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

static const char *TAG = "SERVO_POT";

// ---------- Potentiometer (ADC) ----------
#define POT_ADC_UNIT       ADC_UNIT_1
#define POT_ADC_CHANNEL    ADC_CHANNEL_0        // ESP32-S3: GPIO1
#define POT_ADC_ATTEN      ADC_ATTEN_DB_12      // full 0..3.3 V swing
#define POT_ADC_BITWIDTH   ADC_BITWIDTH_12
#define ADC_MAX_RAW        4095

// ---------- Servo (LEDC PWM) ----------
#define SERVO_GPIO         10
#define SERVO_FREQ_HZ      50                   // 20 ms period
#define LEDC_TIMER         LEDC_TIMER_0
#define LEDC_MODE          LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL       LEDC_CHANNEL_0
#define LEDC_RESOLUTION    LEDC_TIMER_14_BIT    // 16384 steps / 20 ms
#define LEDC_DUTY_MAX      ((1 << 14) - 1)
#define SERVO_MIN_PULSE_US 500                  // SG90 ≈ 0°
#define SERVO_MAX_PULSE_US 2500                 // SG90 ≈ 180°
#define SERVO_PERIOD_US    20000

// ---------- Physical arcs ----------
#define POT_RANGE_DEG      270.0f               // typical rotary pot
#define SERVO_RANGE_DEG    180.0f               // typical hobby servo
// Only the overlapping part of both ranges is used (1:1 mapping).
#define USABLE_RANGE_DEG   (POT_RANGE_DEG < SERVO_RANGE_DEG ? POT_RANGE_DEG : SERVO_RANGE_DEG)

static uint32_t angle_to_duty(float angle_deg)
{
    if (angle_deg < 0.0f)            angle_deg = 0.0f;
    if (angle_deg > SERVO_RANGE_DEG) angle_deg = SERVO_RANGE_DEG;

    float pulse_us = SERVO_MIN_PULSE_US +
        (SERVO_MAX_PULSE_US - SERVO_MIN_PULSE_US) * (angle_deg / SERVO_RANGE_DEG);
    return (uint32_t)((pulse_us / (float)SERVO_PERIOD_US) * (float)(LEDC_DUTY_MAX + 1));
}

static void servo_init(void)
{
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_MODE,
        .timer_num       = LEDC_TIMER,
        .duty_resolution = LEDC_RESOLUTION,
        .freq_hz         = SERVO_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    ledc_channel_config_t channel_cfg = {
        .gpio_num   = SERVO_GPIO,
        .speed_mode = LEDC_MODE,
        .channel    = LEDC_CHANNEL,
        .timer_sel  = LEDC_TIMER,
        .intr_type  = LEDC_INTR_DISABLE,
        .duty       = 0,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel_cfg));
}

static void servo_set_angle(float angle_deg)
{
    uint32_t duty = angle_to_duty(angle_deg);
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL));
}

static adc_oneshot_unit_handle_t pot_adc_init(void)
{
    adc_oneshot_unit_handle_t handle;
    adc_oneshot_unit_init_cfg_t unit_cfg = { .unit_id = POT_ADC_UNIT };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &handle));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = POT_ADC_ATTEN,
        .bitwidth = POT_ADC_BITWIDTH,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(handle, POT_ADC_CHANNEL, &chan_cfg));
    return handle;
}

void app_main(void)
{
    adc_oneshot_unit_handle_t adc = pot_adc_init();
    servo_init();

    ESP_LOGI(TAG, "Pot arc = %.0f°, servo arc = %.0f°, usable (overlap) = %.0f°",
             POT_RANGE_DEG, SERVO_RANGE_DEG, USABLE_RANGE_DEG);

    while (1) {
        int raw = 0;
        ESP_ERROR_CHECK(adc_oneshot_read(adc, POT_ADC_CHANNEL, &raw));

        // Raw ADC -> potentiometer shaft angle on its full arc.
        float pot_angle = ((float)raw / (float)ADC_MAX_RAW) * POT_RANGE_DEG;

        // Cut off the part of the pot arc that the servo cannot follow —
        // only the overlapping [0 .. USABLE_RANGE_DEG] region drives it 1:1.
        float angle = pot_angle;
        if (angle > USABLE_RANGE_DEG) angle = USABLE_RANGE_DEG;
        if (angle < 0.0f)             angle = 0.0f;

        servo_set_angle(angle);

        // Deflection from the leftmost (0°) position.
        ESP_LOGI(TAG, "Відхилення від крайнього лівого положення: %.1f°", angle);

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
