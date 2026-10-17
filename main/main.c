#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "traffic";

// ---------- Налаштування пінів ----------
#define PIN_GREEN   5
#define PIN_YELLOW  6
#define PIN_RED     7
#define PIN_BUTTON  4

// ---------- Тривалості (мілісекунди) ----------
#define T_GREEN_MS          10000UL
#define T_GREEN_BLINK_MS     3000UL
#define T_YELLOW_MS          3000UL
#define T_RED_MS            10000UL
#define T_RED_YELLOW_MS      2000UL
#define BLINK_HALF_PERIOD_MS  500UL
#define DEBOUNCE_MS           200UL

// Роздільна здатність таймера: 1 МГц => 1 тік = 1 мкс
#define TIMER_RESOLUTION_HZ   1000000ULL
#define MS_TO_TICKS(ms)       ((uint64_t)(ms) * 1000ULL)

// ---------- Стани автомата ----------
enum TrafficState {
  STATE_GREEN = 0,
  STATE_GREEN_BLINK,
  STATE_YELLOW,
  STATE_RED,
  STATE_RED_YELLOW,
  STATE_YELLOW_BLINK
};

// ---------- Події ----------
enum TrafficEvent {
  EVT_NONE = 0,
  EVT_STATE_TIMEOUT,
  EVT_BLINK_TICK,
  EVT_BUTTON
};

// ---------- Прапорці подій (встановлюються в ISR) ----------
static volatile bool flagStateTimeout = false;
static volatile bool flagBlinkTick    = false;
static volatile bool flagButton       = false;

// ---------- Хендли таймерів ----------
static gptimer_handle_t stateTimer = NULL;   // one-shot, тривалість поточного стану
static gptimer_handle_t blinkTimer = NULL;   // періодичний 500 мс

// ---------- Стан автомата ----------
static enum TrafficState currentState = STATE_GREEN;
static bool specialMode = false;
static bool blinkPhase  = false;

// ---------- Антидребезг кнопки ----------
static volatile uint32_t lastButtonMs = 0;

// ---------- Утиліти ----------
static inline uint32_t millis_now(void) {
  return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static void setLights(bool g, bool y, bool r) {
  gpio_set_level(PIN_GREEN,  g ? 1 : 0);
  gpio_set_level(PIN_YELLOW, y ? 1 : 0);
  gpio_set_level(PIN_RED,    r ? 1 : 0);
}

// ---------- Керування таймерами ----------
// gptimer_stop/start повертають ESP_ERR_INVALID_STATE, якщо таймер уже
// у відповідному стані — для нас це ОК, ігноруємо.
static inline void safe_stop(gptimer_handle_t t) {
  esp_err_t e = gptimer_stop(t);
  if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) ESP_ERROR_CHECK(e);
}
static inline void safe_start(gptimer_handle_t t) {
  esp_err_t e = gptimer_start(t);
  if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) ESP_ERROR_CHECK(e);
}

static void stateTimer_startOnce(uint32_t ms) {
  safe_stop(stateTimer);
  ESP_ERROR_CHECK(gptimer_set_raw_count(stateTimer, 0));
  gptimer_alarm_config_t alarm = {
    .alarm_count = MS_TO_TICKS(ms),
    .reload_count = 0,
    .flags.auto_reload_on_alarm = false,
  };
  ESP_ERROR_CHECK(gptimer_set_alarm_action(stateTimer, &alarm));
  safe_start(stateTimer);
}

static void stateTimer_stop(void) {
  safe_stop(stateTimer);
}

static void blinkTimer_start(void) {
  safe_stop(blinkTimer);
  ESP_ERROR_CHECK(gptimer_set_raw_count(blinkTimer, 0));
  blinkPhase = false;
  safe_start(blinkTimer);
}

static void blinkTimer_stop(void) {
  safe_stop(blinkTimer);
}

// ---------- ISR таймерів ----------
static bool IRAM_ATTR stateTimer_isr(gptimer_handle_t timer,
                                     const gptimer_alarm_event_data_t *edata,
                                     void *user_ctx) {
  flagStateTimeout = true;
  return false;
}

static bool IRAM_ATTR blinkTimer_isr(gptimer_handle_t timer,
                                     const gptimer_alarm_event_data_t *edata,
                                     void *user_ctx) {
  flagBlinkTick = true;
  return false;
}

// ---------- ISR кнопки ----------
static void IRAM_ATTR button_isr(void *arg) {
  uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);
  if (now - lastButtonMs < DEBOUNCE_MS) return;
  lastButtonMs = now;
  flagButton = true;
}

// ---------- Вхід у стан ----------
static void enterState(enum TrafficState s) {
  currentState = s;

  switch (s) {
    case STATE_GREEN:
      blinkTimer_stop();
      setLights(true, false, false);
      stateTimer_startOnce(T_GREEN_MS);
      ESP_LOGI(TAG, "GREEN");
      break;

    case STATE_GREEN_BLINK:
      blinkPhase = true;
      setLights(true, false, false);
      blinkTimer_start();
      stateTimer_startOnce(T_GREEN_BLINK_MS);
      ESP_LOGI(TAG, "GREEN BLINK");
      break;

    case STATE_YELLOW:
      blinkTimer_stop();
      setLights(false, true, false);
      stateTimer_startOnce(T_YELLOW_MS);
      ESP_LOGI(TAG, "YELLOW");
      break;

    case STATE_RED:
      blinkTimer_stop();
      setLights(false, false, true);
      stateTimer_startOnce(T_RED_MS);
      ESP_LOGI(TAG, "RED");
      break;

    case STATE_RED_YELLOW:
      blinkTimer_stop();
      setLights(false, true, true);
      stateTimer_startOnce(T_RED_YELLOW_MS);
      ESP_LOGI(TAG, "RED+YELLOW");
      break;

    case STATE_YELLOW_BLINK:
      stateTimer_stop();
      blinkPhase = true;
      setLights(false, true, false);
      blinkTimer_start();
      ESP_LOGI(TAG, "YELLOW BLINK (special)");
      break;
  }
}

static enum TrafficState nextNormalState(enum TrafficState s) {
  switch (s) {
    case STATE_GREEN:       return STATE_GREEN_BLINK;
    case STATE_GREEN_BLINK: return STATE_YELLOW;
    case STATE_YELLOW:      return STATE_RED;
    case STATE_RED:         return STATE_RED_YELLOW;
    case STATE_RED_YELLOW:  return STATE_GREEN;
    default:                return STATE_GREEN;
  }
}

// ---------- Автомат ----------
static void handleEvent(enum TrafficEvent evt) {
  switch (evt) {
    case EVT_BUTTON:
      if (!specialMode) {
        specialMode = true;
        enterState(STATE_YELLOW_BLINK);
      } else {
        specialMode = false;
        enterState(STATE_GREEN);
      }
      break;

    case EVT_STATE_TIMEOUT:
      if (!specialMode) {
        enterState(nextNormalState(currentState));
      }
      break;

    case EVT_BLINK_TICK:
      blinkPhase = !blinkPhase;
      if (currentState == STATE_GREEN_BLINK) {
        setLights(blinkPhase, false, false);
      } else if (currentState == STATE_YELLOW_BLINK) {
        setLights(false, blinkPhase, false);
      }
      break;

    default:
      break;
  }
}

// ---------- Ініціалізація периферії ----------
static void gpio_init_all(void) {
  gpio_config_t out = {
    .pin_bit_mask = (1ULL << PIN_GREEN) | (1ULL << PIN_YELLOW) | (1ULL << PIN_RED),
    .mode = GPIO_MODE_OUTPUT,
    .pull_up_en = GPIO_PULLUP_DISABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE,
  };
  ESP_ERROR_CHECK(gpio_config(&out));
  setLights(false, false, false);

  gpio_config_t in = {
    .pin_bit_mask = (1ULL << PIN_BUTTON),
    .mode = GPIO_MODE_INPUT,
    .pull_up_en = GPIO_PULLUP_ENABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_NEGEDGE,
  };
  ESP_ERROR_CHECK(gpio_config(&in));

  esp_err_t err = gpio_install_isr_service(0);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_ERROR_CHECK(err);
  }
  ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_BUTTON, button_isr, NULL));
  ESP_LOGI(TAG, "GPIO ready: G=%d Y=%d R=%d BTN=%d",
           PIN_GREEN, PIN_YELLOW, PIN_RED, PIN_BUTTON);
}

static void timers_init_all(void) {
  gptimer_config_t cfg = {
    .clk_src = GPTIMER_CLK_SRC_DEFAULT,
    .direction = GPTIMER_COUNT_UP,
    .resolution_hz = TIMER_RESOLUTION_HZ,
  };

  // stateTimer: one-shot
  ESP_ERROR_CHECK(gptimer_new_timer(&cfg, &stateTimer));
  gptimer_event_callbacks_t cbs1 = { .on_alarm = stateTimer_isr };
  ESP_ERROR_CHECK(gptimer_register_event_callbacks(stateTimer, &cbs1, NULL));
  ESP_ERROR_CHECK(gptimer_enable(stateTimer));

  // blinkTimer: періодичний 500 мс
  ESP_ERROR_CHECK(gptimer_new_timer(&cfg, &blinkTimer));
  gptimer_event_callbacks_t cbs2 = { .on_alarm = blinkTimer_isr };
  ESP_ERROR_CHECK(gptimer_register_event_callbacks(blinkTimer, &cbs2, NULL));
  gptimer_alarm_config_t blinkAlarm = {
    .alarm_count = MS_TO_TICKS(BLINK_HALF_PERIOD_MS),
    .reload_count = 0,
    .flags.auto_reload_on_alarm = true,
  };
  ESP_ERROR_CHECK(gptimer_set_alarm_action(blinkTimer, &blinkAlarm));
  ESP_ERROR_CHECK(gptimer_enable(blinkTimer));
  ESP_LOGI(TAG, "Timers ready");
}

// ---------- Головний цикл ----------
void app_main(void) {
  gpio_init_all();
  timers_init_all();

  enterState(STATE_GREEN);

  for (;;) {
    if (flagButton) {
      flagButton = false;
      handleEvent(EVT_BUTTON);
    }
    if (flagStateTimeout) {
      flagStateTimeout = false;
      handleEvent(EVT_STATE_TIMEOUT);
    }
    if (flagBlinkTick) {
      flagBlinkTick = false;
      handleEvent(EVT_BLINK_TICK);
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}
