#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#define LED_PIN 18
#define LEDC_CHANNEL 0
#define LEDC_TIMER 0
#define LEDC_FREQUENCY 1000
#define LEDC_RESOLUTION LEDC_TIMER_12_BIT

#define ENGINE_PIN 7
#define LEDC_ENGINE_CHANNEL 1

#define ADC_UNIT ADC_UNIT_1
#define ADC_CHANNEL ADC_CHANNEL_7
#define ADC_ATTEN ADC_ATTEN_DB_12
#define ADC_BITWIDTH ADC_BITWIDTH_12

static ledc_timer_config_t timer_config = {
    .speed_mode = LEDC_LOW_SPEED_MODE,
    .duty_resolution = LEDC_RESOLUTION,
    .timer_num = LEDC_TIMER,
    .freq_hz = LEDC_FREQUENCY,
    .clk_cfg = LEDC_AUTO_CLK
};

static ledc_channel_config_t channel_config = {
    .gpio_num = LED_PIN,
    .speed_mode = LEDC_LOW_SPEED_MODE,
    .channel = LEDC_CHANNEL,
    .timer_sel = LEDC_TIMER,
    .duty = 0,
    .hpoint = 0
};

static ledc_channel_config_t engine_channel_config = {
     .gpio_num = ENGINE_PIN,
    .speed_mode = LEDC_LOW_SPEED_MODE,
    .channel = LEDC_ENGINE_CHANNEL,
    .timer_sel = LEDC_TIMER,
    .duty = 0,
    .hpoint = 0
};

void app_main(void)
{
    ledc_timer_config(&timer_config);
    ledc_channel_config(&channel_config);
    ledc_channel_config(&engine_channel_config);

    adc_oneshot_unit_handle_t adc_handle;
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT,
    };
    adc_oneshot_new_unit(&init_cfg, &adc_handle);

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH,
    };
    adc_oneshot_config_channel(adc_handle, ADC_CHANNEL, &chan_cfg);

    adc_cali_handle_t cali_handle = NULL;
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = ADC_UNIT,
        .chan = ADC_CHANNEL,
        .atten = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH,
    };
    adc_cali_create_scheme_curve_fitting(&cali_cfg, &cali_handle);

    int raw = 0;
    int voltage_mv = 0;

    while (1) {
        adc_oneshot_read(adc_handle, ADC_CHANNEL, &raw);
        adc_cali_raw_to_voltage(cali_handle, raw, &voltage_mv);

        printf("raw=%d  voltage=%d mV\n", raw, voltage_mv);

        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL, raw);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL);

        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_ENGINE_CHANNEL, raw);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_ENGINE_CHANNEL);

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
