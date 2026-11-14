#include "status_led.h"
#include "led_strip.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "status_led";

/* WS2812 has no analogue dimming; scale the colour values instead. */
#define LED_MAX_LEVEL 12

typedef struct {
    uint8_t r, g, b;
    uint32_t on_ms;  /* 0 = solid */
    uint32_t off_ms;
    bool heartbeat;  /* dark at rest; status_led_pulse() flashes it on */
} pattern_t;

/* Blinks are short flashes with a long gap: less glare, easier to read. */
static const pattern_t PATTERNS[] = {
    [STATUS_LED_OFF]             = { 0, 0, 0, 0, 0 },
    [STATUS_LED_BOOT]            = { 255, 255, 255, 0, 0 },
    [STATUS_LED_PORTAL]          = { 0, 0, 255, 100, 900 },
    [STATUS_LED_WIFI_CONNECTING] = { 255, 180, 0, 50, 250 },
    [STATUS_LED_WIFI_FAILED]     = { 255, 0, 0, 100, 900 },
    [STATUS_LED_RUNNING]         = { 0, 255, 0, 0, 0, true },
};

#define PULSE_MS 80

static led_strip_handle_t s_strip;
static esp_timer_handle_t s_blink_timer;
static esp_timer_handle_t s_pulse_timer;
static status_led_state_t s_state;
static bool s_phase_on;

static void show(bool on)
{
    const pattern_t *p = &PATTERNS[s_state];
    if (on) {
        led_strip_set_pixel(s_strip, 0,
                            p->r * LED_MAX_LEVEL / 255,
                            p->g * LED_MAX_LEVEL / 255,
                            p->b * LED_MAX_LEVEL / 255);
    } else {
        led_strip_set_pixel(s_strip, 0, 0, 0, 0);
    }
    led_strip_refresh(s_strip);
}

/* Arm the blink timer for the duration of the current phase. */
static void arm_blink(void)
{
    const pattern_t *p = &PATTERNS[s_state];
    uint32_t ms = s_phase_on ? p->on_ms : p->off_ms;
    esp_timer_start_once(s_blink_timer, (uint64_t)ms * 1000);
}

static void blink_cb(void *arg)
{
    (void)arg;
    s_phase_on = !s_phase_on;
    show(s_phase_on);
    arm_blink();
}

/* End of pulse: return to the resting level for this state. */
static void pulse_cb(void *arg)
{
    (void)arg;
    show(!PATTERNS[s_state].heartbeat);
}

void status_led_init(int gpio)
{
    led_strip_config_t strip_cfg = {
        .strip_gpio_num = gpio,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };
    led_strip_rmt_config_t rmt_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
    };
    esp_err_t err = led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "led_strip init failed: %s", esp_err_to_name(err));
        s_strip = NULL;
        return;
    }

    esp_timer_create_args_t args = {
        .callback = blink_cb,
        .name = "status_led",
    };
    ESP_ERROR_CHECK(esp_timer_create(&args, &s_blink_timer));

    esp_timer_create_args_t pulse_args = {
        .callback = pulse_cb,
        .name = "status_led_pulse",
    };
    ESP_ERROR_CHECK(esp_timer_create(&pulse_args, &s_pulse_timer));

    status_led_set(STATUS_LED_OFF);
}

void status_led_set(status_led_state_t state)
{
    if (!s_strip) return;

    esp_timer_stop(s_blink_timer);
    esp_timer_stop(s_pulse_timer);
    s_state = state;
    s_phase_on = true;
    show(!PATTERNS[state].heartbeat);

    if (PATTERNS[state].on_ms)
        arm_blink();
}

void status_led_pulse(void)
{
    if (!s_strip) return;
    if (PATTERNS[s_state].on_ms != 0) return; /* blinking already */
    if (s_state == STATUS_LED_OFF) return;

    /* Heartbeat states flash on briefly; solid states blink off briefly. */
    show(PATTERNS[s_state].heartbeat);
    esp_timer_stop(s_pulse_timer);
    esp_timer_start_once(s_pulse_timer, (uint64_t)PULSE_MS * 1000);
}
