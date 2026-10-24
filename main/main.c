#include <stdio.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

static const char *TAG = "LDR_LED";

#define LDR_ADC_UNIT        ADC_UNIT_1
#define LDR_ADC_CHANNEL     ADC_CHANNEL_6   // GPIO7 на ESP32-S3 (ADC1_CH6)
#define LDR_ADC_ATTEN       ADC_ATTEN_DB_12 // діапазон ~0..3.3 В
#define LDR_ADC_BITWIDTH    ADC_BITWIDTH_DEFAULT

#define LED_GPIO            GPIO_NUM_5

#define SMA_WINDOW_SIZE     10
#define SAMPLE_PERIOD_MS    100

// Пороги гістерезису (для 12-бітного ADC, 0..4095).
// Схема дільника: VCC -- LDR -- ADC -- R(10k) -- GND.
// Темно  -> опір LDR великий -> низька напруга на ADC -> малі значення.
// Світло -> опір LDR малий   -> висока напруга на ADC -> великі значення.
#define THRESHOLD_DARK      2000  // нижче цього -> темно, вмикаємо LED
#define THRESHOLD_LIGHT     2400   // вище цього -> світло, вимикаємо LED

static uint32_t sma_buffer[SMA_WINDOW_SIZE];
static int sma_index = 0;
static int sma_count = 0;
static uint32_t sma_sum = 0;

static uint32_t sma_update(uint32_t new_sample)
{
    sma_sum -= sma_buffer[sma_index];
    sma_buffer[sma_index] = new_sample;
    sma_sum += new_sample;

    sma_index = (sma_index + 1) % SMA_WINDOW_SIZE;
    if (sma_count < SMA_WINDOW_SIZE) {
        sma_count++;
    }
    return sma_sum / sma_count;
}

void app_main(void)
{
    gpio_config_t led_cfg = {
        .pin_bit_mask = 1ULL << LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&led_cfg));
    gpio_set_level(LED_GPIO, 0);

    adc_oneshot_unit_handle_t adc_handle;
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = LDR_ADC_UNIT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc_handle));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = LDR_ADC_ATTEN,
        .bitwidth = LDR_ADC_BITWIDTH,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, LDR_ADC_CHANNEL, &chan_cfg));

    bool led_on = false;

    while (1) {
        int raw = 0;
        ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, LDR_ADC_CHANNEL, &raw));

        uint32_t filtered = sma_update((uint32_t)raw);

        if (!led_on && filtered < THRESHOLD_DARK) {
            led_on = true;
            gpio_set_level(LED_GPIO, 1);
        } else if (led_on && filtered > THRESHOLD_LIGHT) {
            led_on = false;
            gpio_set_level(LED_GPIO, 0);
        }

        ESP_LOGI(TAG, "raw=%d  sma=%lu  led=%s",
                 raw, (unsigned long)filtered, led_on ? "ON" : "OFF");

        vTaskDelay(pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
    }
}