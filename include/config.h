#pragma once

#include <stdint.h>
#include <stddef.h>

// --- MQTT ---
constexpr char     MQTT_ADDRES[]            = "p01f8d3a.ala.dedicated.aws.emqxcloud.com";
constexpr uint16_t MQTT_TCP_PORT            = 1883;
constexpr char     MQTT_USERNAME[]          = "Temp_sensor";
constexpr char     MQTT_PASSWORD[]          = "anton1111";
constexpr char     MQTT_CLIENT_ID[]         = "esp32c3-sensor-01";
constexpr char     MQTT_TOPIC[]             = "sensor/esp32c3/data";
constexpr uint32_t MQTT_PUBLISH_INTERVAL_MS = 60000;  // 1 минута
constexpr uint16_t MQTT_KEEP_ALIVE_S        = 60;
constexpr uint16_t MQTT_BUFFER_SIZE         = 256;
constexpr size_t   MQTT_HELLO_BUF_LEN       = 128;
constexpr size_t   MQTT_PAYLOAD_BUF_LEN     = 200;

// --- GPRS - укажите APN вашего оператора ---
// Kyivstar: "www.kyivstar.net" / Vodafone UA: "internet" / lifecell: "internet"
constexpr char GPRS_APN[]  = "internet";
constexpr char GPRS_USER[] = "";
constexpr char GPRS_PASS[] = "";

// --- I2C / BME280 ---
constexpr uint8_t  SDA_PIN            = 8;
constexpr uint8_t  SCL_PIN            = 9;
constexpr uint32_t I2C_FREQ_HZ        = 400000;
constexpr uint8_t  BME_ADDR_PRIMARY   = 0x76;
constexpr uint8_t  BME_ADDR_SECONDARY = 0x77;
constexpr float    SEA_LEVEL_HPA      = 1013.25f;

// --- Общие ---
constexpr uint32_t SAMPLE_PERIOD_US        = 1000000;  // 1 с
constexpr uint32_t SERIAL_BAUD             = 9600;
constexpr uint32_t SERIAL_WAIT_TIMEOUT_MS  = 2000;
constexpr uint32_t SERIAL_WAIT_POLL_MS     = 10;

// APB_CLK 80 МГц / 80 => тик таймера 1 МГц (1 мкс)
constexpr uint16_t SAMPLE_TIMER_DIVIDER = 80;

// --- GPS ATGM336H (SoftwareSerial, UART2) ---
constexpr uint8_t  GPS_RX                = 6;  // К этому пину подключаем TX модуля GPS
constexpr uint8_t  GPS_TX                = 7;  // К этому пину подключаем RX модуля GPS
constexpr uint32_t GPS_UART_BAUD         = 9600;
constexpr int      GPS_SW_RX_BUF_BYTES   = 95;
constexpr int      GPS_SW_ISR_BUF_BYTES  = 11;

// Критерии «хорошего» GPS-фикса
constexpr uint32_t GPS_MIN_SATELLITES    = 4;
constexpr uint32_t GPS_MAX_HDOP_X100     = 500;    // HDOP < 5.00 (в сотых)
constexpr uint32_t GPS_HDOP_INVALID      = 9999;   // sentinel для невалидного HDOP
constexpr uint32_t GPS_MAX_FIX_AGE_MS    = 2000;   // данные свежие
constexpr uint32_t GPS_MAX_SPEED_AGE_MS  = 2000;
constexpr float    GPS_SPEED_NOISE_KMPH  = 2.0f;   // шум ниже порога считаем нулём
constexpr uint32_t GPS_PUBLISH_FIX_AGE_MS = 5000;  // для MQTT допускаем чуть более старый фикс

// --- SIM800L (GSM, UART1) ---
constexpr uint8_t  GSM_RX                      = 4;
constexpr uint8_t  GSM_TX                      = 5;
constexpr uint8_t  GSM_RST                     = 3;
constexpr uint32_t GSM_UART_BAUD               = 9600;

// Тайминги аппаратного сброса SIM800L
constexpr uint32_t GSM_RESET_PRE_MS            = 100;
constexpr uint32_t GSM_RESET_PULSE_MS          = 150;
constexpr uint32_t GSM_BOOT_WAIT_MS            = 5000;

// Autobauding (синхронизация скорости AT)
constexpr uint8_t  GSM_AUTOBAUD_RETRIES        = 5;
constexpr uint32_t GSM_AUTOBAUD_DELAY_MS       = 300;

// Таймауты TinyGSM
constexpr uint32_t GSM_TEST_AT_TIMEOUT_MS      = 5000;
constexpr uint32_t GSM_NETWORK_WAIT_TIMEOUT_MS = 60000L;

// Качество сигнала (CSQ): 99 — спецзначение «нет сигнала»; шкала 0..31
constexpr int      GSM_CSQ_NO_SIGNAL           = 99;
constexpr int      GSM_CSQ_MIN_USABLE          = 5;
