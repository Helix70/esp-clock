#include "buttons_hal.h"
#include "driver/gpio.h"
#include "esp_timer.h"

static int s_pin_btn1;
static int s_pin_btn2;
static esp_timer_handle_t s_timer;

static void poll_timer_cb(void *arg)
{
    (void)arg;
    bool b1 = gpio_get_level(s_pin_btn1) == 0;
    bool b2 = gpio_get_level(s_pin_btn2) == 0;
    buttons_poll(b1, b2);
}

int buttons_hal_init(int pin_btn1, int pin_btn2, button_callback_t callback)
{
    s_pin_btn1 = pin_btn1;
    s_pin_btn2 = pin_btn2;

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << pin_btn1) | (1ULL << pin_btn2),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    buttons_init(callback);

    /*
     * Task dispatch, not ISR: esp_timer's ISR runs with the flash cache
     * disabled during WiFi/NVS writes, and a 20 ms debounce poll has no
     * need for ISR latency. (The display scan timer is ISR-dispatched but
     * its whole path is IRAM-resident.)
     */
    esp_timer_create_args_t timer_args = {
        .callback = poll_timer_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "btn_poll",
    };
    esp_err_t err = esp_timer_create(&timer_args, &s_timer);
    if (err != ESP_OK) return -1;

    return esp_timer_start_periodic(s_timer, 20 * 1000) == ESP_OK ? 0 : -1;
}

void buttons_hal_stop(void)
{
    if (s_timer) {
        esp_timer_stop(s_timer);
        esp_timer_delete(s_timer);
        s_timer = NULL;
    }
}
