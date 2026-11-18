#include "buzzer.h"

#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "buzzer";

#define LEDC_MODE    LEDC_LOW_SPEED_MODE
#define LEDC_TIMER   LEDC_TIMER_0
#define LEDC_CHANNEL LEDC_CHANNEL_0
#define LEDC_RES     LEDC_TIMER_10_BIT
#define DUTY_HALF    (1u << (LEDC_RES - 1)) /* 50 %: loudest square wave */

#define FREQ_MIN 100
#define FREQ_MAX 10000

typedef struct {
    buzzer_note_t notes[BUZZER_MAX_NOTES];
    uint8_t count;
} pattern_t;

static QueueHandle_t s_queue;

static void tone_on(uint16_t freq_hz)
{
    if (freq_hz < FREQ_MIN) freq_hz = FREQ_MIN;
    if (freq_hz > FREQ_MAX) freq_hz = FREQ_MAX;
    ledc_set_freq(LEDC_MODE, LEDC_TIMER, freq_hz);
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, DUTY_HALF);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}

static void tone_off(void)
{
    /* Idle low: the NPN is off, so nothing pulls Q1's base down. */
    ledc_stop(LEDC_MODE, LEDC_CHANNEL, 0);
}

static void buzzer_task(void *arg)
{
    (void)arg;
    pattern_t p;

    for (;;) {
        xQueueReceive(s_queue, &p, portMAX_DELAY);

    play:
        for (uint8_t i = 0; i < p.count; i++) {
            if (p.notes[i].freq_hz) tone_on(p.notes[i].freq_hz);
            else tone_off();

            /* Waiting on the queue doubles as the note delay, so a new
             * request cuts in straight away. */
            if (xQueueReceive(s_queue, &p, pdMS_TO_TICKS(p.notes[i].ms)) == pdTRUE) {
                tone_off();
                goto play;
            }
        }
        tone_off();
    }
}

esp_err_t buzzer_init(int gpio)
{
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_MODE,
        .duty_resolution = LEDC_RES,
        .timer_num = LEDC_TIMER,
        .freq_hz = BUZZER_DEFAULT_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer);
    if (err != ESP_OK) return err;

    ledc_channel_config_t ch = {
        .gpio_num = gpio,
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CHANNEL,
        .timer_sel = LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    err = ledc_channel_config(&ch);
    if (err != ESP_OK) return err;

    /* Push-pull into an NPN that pulls Q1's base low: high = sound. The NPN
     * keeps the pin off Q1's 5 V base. */
    tone_off();

    s_queue = xQueueCreate(1, sizeof(pattern_t));
    if (!s_queue) return ESP_ERR_NO_MEM;
    if (xTaskCreate(buzzer_task, "buzzer", 2560, NULL, 4, NULL) != pdPASS)
        return ESP_ERR_NO_MEM;

    ESP_LOGI(TAG, "Buzzer on GPIO%d", gpio);
    return ESP_OK;
}

void buzzer_play(const buzzer_note_t *notes, size_t count)
{
    if (!s_queue || count == 0) return;
    if (count > BUZZER_MAX_NOTES) count = BUZZER_MAX_NOTES;

    pattern_t p = {.count = (uint8_t)count};
    memcpy(p.notes, notes, count * sizeof(*notes));
    xQueueOverwrite(s_queue, &p);
}

void buzzer_beep(uint16_t freq_hz, uint16_t ms)
{
    buzzer_note_t n = {freq_hz, ms};
    buzzer_play(&n, 1);
}

void buzzer_stop(void)
{
    buzzer_note_t rest = {0, 1};
    buzzer_play(&rest, 1);
}
