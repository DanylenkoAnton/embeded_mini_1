#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"

#define TAG "UART_LINK"

// --- Пінаут ---
#define LED_GPIO        GPIO_NUM_2     // світлодіод на стороні ESP32
#define BTN_GPIO        GPIO_NUM_0     // BOOT-кнопка (активна на LOW)

#define UART_PORT       UART_NUM_1
#define UART_TX_PIN     GPIO_NUM_17
#define UART_RX_PIN     GPIO_NUM_18
#define UART_BAUD       9600
#define UART_BUF_SIZE   256

// --- Протокол (один байт — одна команда) ---
#define CMD_TOGGLE_STM  'B'   // ESP → STM: перемкни свій LED
#define CMD_TOGGLE_ESP  'L'   // STM → ESP: перемкни свій LED

static QueueHandle_t btn_evt_queue = NULL;

static void IRAM_ATTR btn_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    xQueueSendFromISR(btn_evt_queue, &gpio_num, NULL);
}

// Задача 1: обробка кнопки ESP32 → відправка команди в STM32
static void button_task(void *arg)
{
    uint32_t gpio_num;
    TickType_t last_tick = 0;
    while (1) {
        if (xQueueReceive(btn_evt_queue, &gpio_num, portMAX_DELAY)) {
            // простий debounce — 200 мс
            TickType_t now = xTaskGetTickCount();
            if ((now - last_tick) < pdMS_TO_TICKS(200)) continue;
            last_tick = now;

            char cmd = CMD_TOGGLE_STM;
            uart_write_bytes(UART_PORT, &cmd, 1);
            ESP_LOGI(TAG, "TX -> STM: '%c'", cmd);
        }
    }
}

// Задача 2: приймання команди від STM32 → перемикання LED ESP32
static void uart_rx_task(void *arg)
{
    uint8_t data[UART_BUF_SIZE];
    bool led_state = false;
    while (1) {
        int len = uart_read_bytes(UART_PORT, data, sizeof(data), pdMS_TO_TICKS(100));
        for (int i = 0; i < len; i++) {
            if (data[i] == CMD_TOGGLE_ESP) {
                led_state = !led_state;
                gpio_set_level(LED_GPIO, led_state);
                ESP_LOGI(TAG, "RX <- STM: '%c' -> LED=%d", data[i], led_state);
            }
        }
    }
}

static void gpio_init(void)
{
    gpio_config_t led_cfg = {
        .pin_bit_mask = 1ULL << LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&led_cfg);
    gpio_set_level(LED_GPIO, 0);

    gpio_config_t btn_cfg = {
        .pin_bit_mask = 1ULL << BTN_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&btn_cfg);

    btn_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BTN_GPIO, btn_isr_handler, (void *)BTN_GPIO);
}

static void uart_init(void)
{
    uart_config_t cfg = {
        .baud_rate = UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_driver_install(UART_PORT, UART_BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(UART_PORT, &cfg);
    uart_set_pin(UART_PORT, UART_TX_PIN, UART_RX_PIN,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

void app_main(void)
{
    gpio_init();
    uart_init();
    xTaskCreate(button_task,  "btn_task", 2048, NULL, 10, NULL);
    xTaskCreate(uart_rx_task, "rx_task",  2048, NULL, 10, NULL);
    ESP_LOGI(TAG, "ready: TX=%d RX=%d LED=%d BTN=%d",
             UART_TX_PIN, UART_RX_PIN, LED_GPIO, BTN_GPIO);
}
