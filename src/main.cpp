#include <Arduino.h>
#include <esp_task_wdt.h>

// --- Конфигурация вентилятора / реле ---------------------------------------

// GPIO для управления ключом вентилятора (MOSFET-затвор или вход реле).
// GPIO 10 на ESP32-C3-DevKitM-1 свободен и не является критичным strapping-пином.
constexpr uint8_t  FAN_PIN             = 10;

// Активный уровень: 1 = HIGH (прямое управление MOSFET или реле «активный HIGH»),
// 0 = LOW (большинство опторазвязанных релейных модулей включается низким уровнем).
constexpr uint8_t  FAN_ACTIVE_LEVEL    = 1;

// Период цикла и длительность включения.
// Период = 1 час, включение = 15 минут. OFF-фаза считается автоматически.
constexpr uint32_t FAN_CYCLE_PERIOD_MS = 60UL * 60UL * 1000UL;   // 1 час
constexpr uint32_t FAN_ON_DURATION_MS  = 15UL * 60UL * 1000UL;   // 15 минут
constexpr uint32_t FAN_OFF_DURATION_MS = FAN_CYCLE_PERIOD_MS - FAN_ON_DURATION_MS;
static_assert(FAN_ON_DURATION_MS <= FAN_CYCLE_PERIOD_MS,
              "FAN_ON_DURATION_MS не может быть больше FAN_CYCLE_PERIOD_MS");

// Аппаратный таймер №1 (у ESP32-C3 всего 2 независимых таймера: 0 и 1).
constexpr uint8_t  FAN_TIMER_INDEX     = 1;
// APB_CLK 80 МГц / 80 => тик таймера 1 мкс.
constexpr uint16_t FAN_TIMER_DIVIDER   = 80;

// --- LED-индикаторы, дублирующие текущее состояние вентилятора -------------
// Один горит в фазе ON, другой — в фазе OFF. Активны по HIGH.
constexpr uint8_t  FAN_LED_ON_PIN      = 0;   // «вентилятор работает»
constexpr uint8_t  FAN_LED_OFF_PIN     = 1;   // «вентилятор отдыхает»
constexpr uint8_t  LED_ACTIVE_LEVEL    = 1;

// --- Watchdog --------------------------------------------------------------
// Таймаут выбран больше периода loop() (loop пустой, крутится быстро),
// но достаточно малым, чтобы зависание быстро приводило к перезагрузке.
constexpr uint32_t WDT_TIMEOUT_S       = 10;

// ---------------------------------------------------------------------------

static hw_timer_t*    fanTimer      = nullptr;
static volatile bool  fanIsOn       = false;
static volatile bool  fanStateDirty = false;  // флаг «состояние изменилось» для диагностики
static volatile uint32_t fanCycleCount = 0;   // счётчик полных ON-включений с момента загрузки

static inline void fanWritePin(bool on) {
  // digitalWrite на ESP32 Arduino безопасен из ISR для стандартных GPIO.
  digitalWrite(FAN_PIN, on ? FAN_ACTIVE_LEVEL : !FAN_ACTIVE_LEVEL);
}

static inline void fanWriteLeds(bool on) {
  digitalWrite(FAN_LED_ON_PIN,  on ? LED_ACTIVE_LEVEL : !LED_ACTIVE_LEVEL);
  digitalWrite(FAN_LED_OFF_PIN, on ? !LED_ACTIVE_LEVEL : LED_ACTIVE_LEVEL);
}

// ISR аппаратного таймера: переключает состояние вентилятора + LED-индикаторов
// и перезаряжает таймер на длительность следующей фазы. Цикл поддерживает сам себя,
// без участия loop() или FreeRTOS-задач.
static void IRAM_ATTR onFanTimer() {
  fanIsOn = !fanIsOn;
  if (fanIsOn) fanCycleCount++;  // считаем каждый вход в ON-фазу
  const uint64_t next_us = (fanIsOn ? FAN_ON_DURATION_MS : FAN_OFF_DURATION_MS) * 1000ULL;
  timerAlarmWrite(fanTimer, next_us, true);
  fanStateDirty = true;
}

// Инициализация модуля вентилятора.
// Защита от повторного запуска: повторный вызов fanInit() — no-op, иначе ISR
// зарегистрировался бы дважды и состояние бы «дёргалось» на каждом тике.
void fanInit() {
  static bool initialized = false;
  if (initialized) {
    Serial.println(F("[FAN] fanInit() уже выполнен — повторный вызов проигнорирован."));
    return;
  }
  initialized = true;

  pinMode(FAN_PIN, OUTPUT);
  pinMode(FAN_LED_ON_PIN, OUTPUT);
  pinMode(FAN_LED_OFF_PIN, OUTPUT);

  fanIsOn = true;
  fanWritePin(fanIsOn);   // стартуем сразу с ON-фазы (15 минут работы при включении)
  fanWriteLeds(fanIsOn);
  fanCycleCount = 1;

  // Таймер FAN_TIMER_INDEX, счёт вверх, тик = 1 мкс.
  fanTimer = timerBegin(FAN_TIMER_INDEX, FAN_TIMER_DIVIDER, true);
  // edge=false: ESP32-C3 поддерживает только уровневое прерывание.
  timerAttachInterrupt(fanTimer, &onFanTimer, false);
  // Первая тревога — через ON-длительность: по её срабатыванию ISR выключит вентилятор.
  timerAlarmWrite(fanTimer, (uint64_t)FAN_ON_DURATION_MS * 1000ULL, true);
  timerAlarmEnable(fanTimer);
}

// Форматирование времени в HH:MM:SS из miллисекунд с момента загрузки
static void formatUptime(uint32_t ms, char* out, size_t out_len) {
  const uint32_t s = ms / 1000UL;
  snprintf(out, out_len, "%02lu:%02lu:%02lu",
           (unsigned long)(s / 3600UL),
           (unsigned long)((s / 60UL) % 60UL),
           (unsigned long)(s % 60UL));
}

static void logStateChange() {
  char ts[16];
  formatUptime(millis(), ts, sizeof(ts));
  Serial.printf("[%s #%lu] FAN -> %s\n",
                ts,
                (unsigned long)fanCycleCount,
                fanIsOn ? "ON" : "OFF");
}

// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 2000) { /* ждём USB CDC enum */ }

  // Watchdog: кормим его из loop(); зависший loop приведёт к panic + reset.
  esp_task_wdt_init(WDT_TIMEOUT_S, true);
  esp_task_wdt_add(NULL);  // подписываем текущую (loop) задачу

  Serial.println(F("=========================================="));
  Serial.printf("[BOOT] FAN ctrl: ON=%lu мс, OFF=%lu мс, период=%lu мс\n",
                (unsigned long)FAN_ON_DURATION_MS,
                (unsigned long)FAN_OFF_DURATION_MS,
                (unsigned long)FAN_CYCLE_PERIOD_MS);
  Serial.printf("[BOOT] FAN_PIN=%u, LED_ON=%u, LED_OFF=%u, WDT=%lus\n",
                FAN_PIN, FAN_LED_ON_PIN, FAN_LED_OFF_PIN,
                (unsigned long)WDT_TIMEOUT_S);
  Serial.println(F("=========================================="));

  fanInit();
  logStateChange();  // печатаем стартовое состояние как «цикл #1»
}

void loop() {
  // Главное занятие loop() — не блокироваться и кормить watchdog.
  esp_task_wdt_reset();

  // Логика вентилятора полностью живёт в ISR; loop лишь печатает смену состояния.
  if (fanStateDirty) {
    fanStateDirty = false;
    fanWritePin(fanIsOn);
    fanWriteLeds(fanIsOn);
    logStateChange();
  }
}
