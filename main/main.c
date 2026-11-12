#include <stdio.h>
#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/ledc.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include <esp_log.h>

// ---- Light sensor (LDR via ADC) -------------------------------------------
#define LDR_ADC_UNIT        ADC_UNIT_1
#define LDR_ADC_CHANNEL     ADC_CHANNEL_8      // GPIO 9
#define LDR_ADC_ATTEN       ADC_ATTEN_DB_12
#define LDR_ADC_BITWIDTH    ADC_BITWIDTH_12

#define LDR_MIN_MV          50
#define LDR_MAX_MV          3000

// ---- Servo (PWM via LEDC) -------------------------------------------------
#define SERVO_GPIO          14
#define SERVO_FREQ_HZ       50
#define SERVO_PERIOD_US     (1000000 / SERVO_FREQ_HZ)
#define SERVO_TIMER         LEDC_TIMER_0
#define SERVO_CHANNEL       LEDC_CHANNEL_0
#define SERVO_RESOLUTION    LEDC_TIMER_12_BIT
#define SERVO_MAX_DUTY      ((1 << SERVO_RESOLUTION) - 1)

#define SERVO_MIN_DEG       10
#define SERVO_MAX_DEG       170
#define SERVO_MIN_PULSE_US  500
#define SERVO_MAX_PULSE_US  2500

// ---- Main loop ------------------------------------------------------------
#define LOOP_PERIOD_MS      100

static const char *TAG = "light_servo";

static adc_oneshot_unit_handle_t adc_handle;
static adc_cali_handle_t         cali_handle;


static void ldr_init(void)
{
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = LDR_ADC_UNIT,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc_handle));

    adc_oneshot_chan_cfg_t channel_cfg = {
        .atten    = LDR_ADC_ATTEN,
        .bitwidth = LDR_ADC_BITWIDTH,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, LDR_ADC_CHANNEL, &channel_cfg));

    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id  = LDR_ADC_UNIT,
        .chan     = LDR_ADC_CHANNEL,
        .atten    = LDR_ADC_ATTEN,
        .bitwidth = LDR_ADC_BITWIDTH,
    };
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_cfg, &cali_handle));
}

static void servo_init(void)
{
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = SERVO_TIMER,
        .duty_resolution = SERVO_RESOLUTION,
        .freq_hz         = SERVO_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    ledc_channel_config_t channel_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = SERVO_CHANNEL,
        .timer_sel  = SERVO_TIMER,
        .intr_type  = LEDC_INTR_DISABLE,
        .gpio_num   = SERVO_GPIO,
        .duty       = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel_cfg));
}

static int clamp(int value, int min, int max)
{
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

static int map_range(int value, int in_min, int in_max, int out_min, int out_max)
{
    if (in_min == in_max) return out_max;
    value = clamp(value, in_min, in_max);
    return out_min + (value - in_min) * (out_max - out_min) / (in_max - in_min);
}

static int ldr_read_mv(void)
{
    int raw;
    int mv;
    ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, LDR_ADC_CHANNEL, &raw));
    ESP_ERROR_CHECK(adc_cali_raw_to_voltage(cali_handle, raw, &mv));
    return mv;
}

static void servo_set_angle(int degrees)
{
    degrees = clamp(degrees, SERVO_MIN_DEG, SERVO_MAX_DEG);

    int pulse_us = map_range(degrees,
                             SERVO_MIN_DEG,      SERVO_MAX_DEG,
                             SERVO_MIN_PULSE_US, SERVO_MAX_PULSE_US);

    uint32_t duty = (uint32_t)pulse_us * SERVO_MAX_DUTY / SERVO_PERIOD_US;

    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, SERVO_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, SERVO_CHANNEL));
}

void app_main(void)
{
    ldr_init();
    servo_init();

    while (1) {
        int light_mv = ldr_read_mv();
        int angle    = map_range(light_mv,
                                 LDR_MIN_MV,    LDR_MAX_MV,
                                 SERVO_MIN_DEG, SERVO_MAX_DEG);

        servo_set_angle(angle);

        ESP_LOGI(TAG, "light=%d mV  angle=%d deg", light_mv, angle);
        vTaskDelay(pdMS_TO_TICKS(LOOP_PERIOD_MS));
    }
}
