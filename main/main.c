#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

/*
 * ADC configuration summary
 * -------------------------
 *  MCU            : ESP32
 *  ADC unit       : ADC1
 *  Channel        : ADC1_CH6  (GPIO34)   <- potentiometer wiper
 *  Pot wiring     : 3V3 -- pot top, GND -- pot bottom, wiper -> GPIO34
 *  Resolution     : 12 bit  (0 .. 4095)
 *  Attenuation    : 12 dB   (full scale ~0..3100 mV)
 *  Vref (nominal) : 3300 mV (used for the manual calculation)
 *
 *  Manual formula : U_manual = RAW * Vref / (2^N - 1)
 *                            = RAW * 3300 / 4095
 *  Calibrated U   : returned by esp_adc_cali_raw_to_voltage()
 *                   (uses the per-chip eFuse calibration curve)
 */

#define ADC_UNIT              ADC_UNIT_1
#define ADC_CHANNEL           ADC_CHANNEL_6      /* GPIO34 */
#define ADC_ATTEN             ADC_ATTEN_DB_12    /* ~0..3.1 V */
#define ADC_BITWIDTH          ADC_BITWIDTH_12    /* 12 bit -> max 4095 */
#define ADC_MAX_RAW           4095
#define ADC_VREF_MV           3300               /* nominal reference, mV */

#define SAMPLE_PERIOD_MS      100
#define SAMPLE_COUNT          20                 /* how many rows to print */

static const char *TAG = "pot_adc";

static bool adc_calibration_init(adc_unit_t unit,
                                 adc_channel_t channel,
                                 adc_atten_t atten,
                                 adc_cali_handle_t *out_handle)
{
    adc_cali_handle_t handle = NULL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (!calibrated) {
        ESP_LOGI(TAG, "calibration scheme: Curve Fitting");
        adc_cali_curve_fitting_config_t cfg = {
            .unit_id  = unit,
            .chan     = channel,
            .atten    = atten,
            .bitwidth = ADC_BITWIDTH,
        };
        if (adc_cali_create_scheme_curve_fitting(&cfg, &handle) == ESP_OK) {
            calibrated = true;
        }
    }
#endif
#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!calibrated) {
        ESP_LOGI(TAG, "calibration scheme: Line Fitting");
        adc_cali_line_fitting_config_t cfg = {
            .unit_id  = unit,
            .atten    = atten,
            .bitwidth = ADC_BITWIDTH,
        };
        if (adc_cali_create_scheme_line_fitting(&cfg, &handle) == ESP_OK) {
            calibrated = true;
        }
    }
#endif

    *out_handle = handle;
    if (!calibrated) {
        ESP_LOGW(TAG, "ADC calibration not available on this chip");
    }
    return calibrated;
}

void app_main(void)
{
    adc_oneshot_unit_handle_t adc_handle;
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_cfg, &adc_handle));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH,
        .atten    = ADC_ATTEN,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ADC_CHANNEL, &chan_cfg));

    adc_cali_handle_t cali_handle = NULL;
    bool cali_ok = adc_calibration_init(ADC_UNIT, ADC_CHANNEL, ADC_ATTEN, &cali_handle);

    printf("\n");
    printf("ADC config: unit=ADC1  ch=GPIO34  atten=12dB  bitwidth=12  Vref=%d mV\n",
           ADC_VREF_MV);
    printf("\n");
    printf(" RAW   U_manual(mV)   U_cali(mV)   Error(%%)\n");
    printf("------------------------------------------\n");

    for (int i = 0; i < SAMPLE_COUNT; ++i) {
        int raw = 0;
        ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, ADC_CHANNEL, &raw));

        /* Manual voltage from RAW using nominal Vref and full-scale code */
        float u_manual = ((float)raw * ADC_VREF_MV) / (float)ADC_MAX_RAW;

        int u_cali = 0;
        if (cali_ok) {
            ESP_ERROR_CHECK(adc_cali_raw_to_voltage(cali_handle, raw, &u_cali));
        }

        float err = 0.0f;
        if (cali_ok && u_cali > 0) {
            err = ((u_manual - (float)u_cali) / (float)u_cali) * 100.0f;
            if (err < 0) err = -err;
        }

        printf("%4d      %6.1f        %5d       %5.2f\n",
               raw, u_manual, u_cali, err);

        vTaskDelay(pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
    }

    printf("------------------------------------------\n");

    if (cali_ok) {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        adc_cali_delete_scheme_curve_fitting(cali_handle);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
        adc_cali_delete_scheme_line_fitting(cali_handle);
#endif
    }
    ESP_ERROR_CHECK(adc_oneshot_del_unit(adc_handle));
}
