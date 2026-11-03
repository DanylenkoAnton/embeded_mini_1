#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "melody";

#define BUZZER_GPIO             GPIO_NUM_4
#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL            LEDC_CHANNEL_0
#define LEDC_DUTY_RES           LEDC_TIMER_10_BIT
#define LEDC_DUTY_SILENT        0
#define LEDC_DUTY_SOUND         512
#define LEDC_BASE_FREQ_HZ       1000

#define NOTE_C4   262
#define NOTE_D4   294
#define NOTE_E4   330
#define NOTE_F4   349
#define NOTE_G4   392
#define NOTE_A4   440
#define NOTE_B4   494
#define NOTE_C5   523
#define NOTE_D5   587
#define NOTE_E5   659
#define NOTE_REST 0

#define BEAT_MS   350
#define GAP_MS    40

typedef struct {
    int freq_hz;
    int duration_ms;
} note_t;

typedef struct {
    const char *name;
    const note_t *notes;
    size_t length;
} melody_t;

static const note_t baby_shark[] = {
    {NOTE_C4, BEAT_MS}, {NOTE_D4, BEAT_MS}, {NOTE_E4, BEAT_MS}, {NOTE_REST, BEAT_MS/2},
    {NOTE_C4, BEAT_MS}, {NOTE_D4, BEAT_MS}, {NOTE_E4, BEAT_MS}, {NOTE_REST, BEAT_MS/2},
    {NOTE_C4, BEAT_MS}, {NOTE_D4, BEAT_MS}, {NOTE_E4, BEAT_MS},
};

static const note_t jingle_bells[] = {
    {NOTE_E4, BEAT_MS}, {NOTE_E4, BEAT_MS}, {NOTE_E4, BEAT_MS*2},
    {NOTE_E4, BEAT_MS}, {NOTE_E4, BEAT_MS}, {NOTE_E4, BEAT_MS*2},
    {NOTE_E4, BEAT_MS}, {NOTE_G4, BEAT_MS}, {NOTE_C4, BEAT_MS},
    {NOTE_D4, BEAT_MS}, {NOTE_E4, BEAT_MS*2},
};

static const note_t we_will_rock_you[] = {
    {NOTE_C4, BEAT_MS}, {NOTE_C4, BEAT_MS}, {NOTE_D4, BEAT_MS}, {NOTE_REST, BEAT_MS/2},
    {NOTE_C4, BEAT_MS}, {NOTE_C4, BEAT_MS}, {NOTE_D4, BEAT_MS}, {NOTE_REST, BEAT_MS/2},
    {NOTE_C4, BEAT_MS}, {NOTE_C4, BEAT_MS}, {NOTE_D4, BEAT_MS}, {NOTE_E4, BEAT_MS*2},
};

static const note_t twinkle_twinkle[] = {
    {NOTE_C4, BEAT_MS}, {NOTE_C4, BEAT_MS}, {NOTE_G4, BEAT_MS}, {NOTE_G4, BEAT_MS},
    {NOTE_A4, BEAT_MS}, {NOTE_A4, BEAT_MS}, {NOTE_G4, BEAT_MS*2},
    {NOTE_F4, BEAT_MS}, {NOTE_F4, BEAT_MS}, {NOTE_E4, BEAT_MS}, {NOTE_E4, BEAT_MS},
    {NOTE_D4, BEAT_MS}, {NOTE_D4, BEAT_MS}, {NOTE_C4, BEAT_MS*2},
};

static const note_t old_macdonald[] = {
    {NOTE_G4, BEAT_MS}, {NOTE_G4, BEAT_MS}, {NOTE_G4, BEAT_MS},
    {NOTE_D4, BEAT_MS}, {NOTE_E4, BEAT_MS}, {NOTE_E4, BEAT_MS}, {NOTE_D4, BEAT_MS*2},
};

static const melody_t melodies[] = {
    {"Baby Shark",              baby_shark,      sizeof(baby_shark)      / sizeof(note_t)},
    {"Jingle Bells",            jingle_bells,    sizeof(jingle_bells)    / sizeof(note_t)},
    {"We Will Rock You",        we_will_rock_you,sizeof(we_will_rock_you)/ sizeof(note_t)},
    {"Twinkle Twinkle Little Star", twinkle_twinkle, sizeof(twinkle_twinkle) / sizeof(note_t)},
    {"Old MacDonald",           old_macdonald,   sizeof(old_macdonald)   / sizeof(note_t)},
};

static void buzzer_init(void)
{
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_MODE,
        .timer_num       = LEDC_TIMER,
        .duty_resolution = LEDC_DUTY_RES,
        .freq_hz         = LEDC_BASE_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    ledc_channel_config_t channel_cfg = {
        .speed_mode = LEDC_MODE,
        .channel    = LEDC_CHANNEL,
        .timer_sel  = LEDC_TIMER,
        .intr_type  = LEDC_INTR_DISABLE,
        .gpio_num   = BUZZER_GPIO,
        .duty       = LEDC_DUTY_SILENT,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel_cfg));
}

static void buzzer_set_silent(void)
{
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, LEDC_DUTY_SILENT);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}

static void play_note(int freq_hz, int duration_ms)
{
    if (freq_hz <= 0) {
        buzzer_set_silent();
        vTaskDelay(pdMS_TO_TICKS(duration_ms));
        return;
    }

    ESP_ERROR_CHECK(ledc_set_freq(LEDC_MODE, LEDC_TIMER, freq_hz));
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, LEDC_DUTY_SOUND);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);

    vTaskDelay(pdMS_TO_TICKS(duration_ms));

    buzzer_set_silent();
    vTaskDelay(pdMS_TO_TICKS(GAP_MS));
}

static void play_melody(const melody_t *m)
{
    ESP_LOGI(TAG, "Playing: %s (%u notes)", m->name, (unsigned)m->length);
    for (size_t i = 0; i < m->length; i++) {
        play_note(m->notes[i].freq_hz, m->notes[i].duration_ms);
    }
    buzzer_set_silent();
}

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-S3 melody synthesizer starting on GPIO%d", BUZZER_GPIO);
    buzzer_init();

    const size_t count = sizeof(melodies) / sizeof(melodies[0]);
    while (1) {
        for (size_t i = 0; i < count; i++) {
            play_melody(&melodies[i]);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}
