#include "aip33628.h"
#include "buttons_hal.h"
#include "buzzer.h"
#include "captive_portal.h"
#include "clock_fx.h"
#include "clock_ntp.h"
#include "clock_sensors.h"
#include "clock_settings.h"
#include "clock_time.h"
#include "hu058.h"
#include "status_led.h"
#include "web_server.h"
#include "wifi_manager.h"

#include "driver/gptimer.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *TAG = "clock";

static aip33628_t drv_hours;
static aip33628_t drv_minutes;
static hu058_display_t display;
static clock_settings_t settings;
static gptimer_handle_t scan_timer;

/* Main loop period. Fast enough for smooth colour effects and colon blink. */
#define TICK_MS 20

/* How long the date and temperature each stay up when cycling. */
#define CYCLE_SHOW_MS 3000

/* Sensors are read this often; brightness and temperature use the result. */
#define SENSOR_PERIOD_MS 500

/*
 * Auto brightness maps LDR resistance onto the min..max range on a log
 * scale: at or above LDR_DARK_OHMS is the minimum, at or below
 * LDR_BRIGHT_OHMS the maximum. LDR resistance falls roughly as a power of
 * illuminance, so log resistance tracks perceived light level.
 */
#define LDR_DARK_OHMS 200000.0f
#define LDR_BRIGHT_OHMS 2000.0f
#define LIGHT_SMOOTHING 0.2f   /* EMA weight of each new sample */
#define LEVEL_HYSTERESIS 0.6f  /* levels the target must move before a step */

typedef enum {
  VIEW_TIME,
  VIEW_TEMPERATURE,
  VIEW_DATE,
  VIEW_COUNT,
} view_t;

/* View picked with button 1. Time is the default and allows cycling. */
static volatile view_t pinned_view = VIEW_TIME;
static volatile bool enter_portal_request = false;
static volatile bool factory_reset_request = false;
static volatile bool ota_active = false;

static float light_fraction = NAN; /* smoothed, 0 = dark, 1 = bright */
static float auto_level = NAN;     /* brightness level last applied */
static float temperature_c = NAN;

/*
 * Display scan runs on its own GPTimer rather than esp_timer. With an
 * ISR-dispatched esp_timer at this rate, every task-dispatched alarm
 * (20 ms button poll, LED blink: all multiples of the scan period) lands on
 * the same alarm tick, esp_timer's ISR reports "ISR timers processed" and
 * never notifies its task, so task-dispatched callbacks starve forever.
 */
static bool IRAM_ATTR scan_timer_cb(gptimer_handle_t timer,
                                    const gptimer_alarm_event_data_t *edata,
                                    void *arg) {
  (void)timer;
  (void)edata;
  (void)arg;
  hu058_scan_tick(&display);
  return false; /* no task woken */
}

static void scan_timer_start(void) {
  gptimer_config_t cfg = {
      .clk_src = GPTIMER_CLK_SRC_DEFAULT,
      .direction = GPTIMER_COUNT_UP,
      .resolution_hz = 1000 * 1000, /* 1 us ticks */
  };
  ESP_ERROR_CHECK(gptimer_new_timer(&cfg, &scan_timer));

  gptimer_event_callbacks_t cbs = {.on_alarm = scan_timer_cb};
  ESP_ERROR_CHECK(gptimer_register_event_callbacks(scan_timer, &cbs, NULL));

  gptimer_alarm_config_t alarm = {
      .alarm_count = HU058_UNIT_US,
      .reload_count = 0,
      .flags.auto_reload_on_alarm = true,
  };
  ESP_ERROR_CHECK(gptimer_set_alarm_action(scan_timer, &alarm));
  ESP_ERROR_CHECK(gptimer_enable(scan_timer));
  ESP_ERROR_CHECK(gptimer_start(scan_timer));
}

/* Buzzer sounds. buzzer_play() is a no-op if the buzzer is not fitted. */
static const buzzer_note_t SOUND_KEY[] = {{BUZZER_DEFAULT_HZ, 25}};
static const buzzer_note_t SOUND_HELD[] = {
    {BUZZER_DEFAULT_HZ, 80}, {0, 60}, {BUZZER_DEFAULT_HZ, 80}};
static const buzzer_note_t SOUND_CHIME[] = {
    {BUZZER_DEFAULT_HZ, 120}, {0, 120}, {BUZZER_DEFAULT_HZ, 120}};
static const buzzer_note_t SOUND_BOOT[] = {{BUZZER_DEFAULT_HZ, 60}};

#define PLAY(sound) buzzer_play((sound), sizeof(sound) / sizeof((sound)[0]))

static void key_sound(const buzzer_note_t *notes, size_t count) {
  if (settings.key_beep)
    buzzer_play(notes, count);
}

static void button_handler(button_event_t event) {
  switch (event) {
  case BUTTON_1_PRESS:
    pinned_view = (view_t)((pinned_view + 1) % VIEW_COUNT);
    key_sound(SOUND_KEY, 1);
    break;
  case BUTTON_2_PRESS:
    key_sound(SOUND_KEY, 1);
    break;
  case BUTTON_1_HELD:
    enter_portal_request = true;
    key_sound(SOUND_HELD, 3);
    break;
  case BUTTON_BOTH_HELD:
    factory_reset_request = true;
    key_sound(SOUND_HELD, 3);
    break;
  default:
    break;
  }
}

static void ota_status(bool active) { ota_active = active; }

/* Settings saved on the web page arrive on the server task and are picked
 * up by the main loop, which owns `settings`. */
static portMUX_TYPE pending_lock = portMUX_INITIALIZER_UNLOCKED;
static clock_settings_t pending_settings;
static volatile bool settings_pending;

static void web_settings_saved(const clock_settings_t *s) {
  taskENTER_CRITICAL(&pending_lock);
  pending_settings = *s;
  settings_pending = true;
  taskEXIT_CRITICAL(&pending_lock);
}

static void apply_pending_settings(void) {
  if (!settings_pending)
    return;

  clock_settings_t s;
  taskENTER_CRITICAL(&pending_lock);
  s = pending_settings;
  settings_pending = false;
  taskEXIT_CRITICAL(&pending_lock);

  bool ntp_changed = strcmp(s.ntp_server, settings.ntp_server) != 0;
  settings = s;
  setenv("TZ", settings.tz, 1);
  tzset();
  hu058_set_time_format(&display, settings.time_24h ? HU058_24H : HU058_12H);
  auto_level = NAN; /* take the new brightness range at once */
  if (ntp_changed)
    clock_ntp_set_server(settings.ntp_server);
  ESP_LOGI(TAG, "Settings applied");
}

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static void apply_colors(uint32_t ms) {
  clock_fx_apply(&display, (clock_fx_mode_t)settings.color_mode,
                 settings.colors, settings.effect_speed, ms);
}

/* Levels 1..16 map one-to-one onto the driver's 16 current steps, so every
 * level is a visible change. */
static void set_brightness_level(float level) {
  long step = lroundf(level) - 1;
  if (step < 0) step = 0;
  if (step > HU058_CURRENT_MAX) step = HU058_CURRENT_MAX;
  hu058_set_brightness(&display, (uint8_t)step);
}

/* Local time minus UTC, in seconds (newlib's struct tm has no tm_gmtoff). */
static int32_t utc_offset(time_t utc) {
  struct tm lt;
  localtime_r(&utc, &lt);
  /* Days since the epoch for the local calendar date (days-from-civil). */
  int y = lt.tm_year + 1900, m = lt.tm_mon + 1;
  y -= m <= 2;
  int era = (y >= 0 ? y : y - 399) / 400;
  int yoe = y - era * 400;
  int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + lt.tm_mday - 1;
  int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  int64_t days = (int64_t)era * 146097 + doe - 719468;
  int64_t local = days * 86400 + lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec;
  return (int32_t)(local - (int64_t)utc);
}

/* Hand the web page what the display is showing. */
static void publish_status(void) {
  web_server_status_t st = {.temp_c = temperature_c, .source = "none"};
  int usec;
  st.time_valid = clock_time_now(&st.utc, &usec);
  st.captured_us = esp_timer_get_time();
  st.usec = usec;
  if (st.time_valid) {
    st.utc_offset_s = utc_offset(st.utc);
    st.source = clock_time_source() == CLOCK_TIME_RTC ? "rtc" : "ntp";
  }
  web_server_publish_status(&st);
}

static void update_sensors(void) {
  temperature_c = clock_sensors_temperature_c();
  if (!isnan(temperature_c))
    temperature_c += (float)settings.temp_offset_tenths / 10.0f;

  float ohms = clock_sensors_ldr_ohms();
  if (isnan(ohms))
    return;

  float frac = (log10f(LDR_DARK_OHMS) - log10f(ohms)) /
               (log10f(LDR_DARK_OHMS) - log10f(LDR_BRIGHT_OHMS));
  if (frac < 0.0f) frac = 0.0f;
  if (frac > 1.0f) frac = 1.0f;

  light_fraction = isnan(light_fraction)
                       ? frac
                       : light_fraction + LIGHT_SMOOTHING * (frac - light_fraction);
}

static void update_brightness(void) {
  if (!settings.auto_brightness) {
    set_brightness_level(settings.brightness);
    return;
  }

  /* No usable light reading: fall back to the maximum. */
  float frac = isnan(light_fraction) ? 1.0f : light_fraction;
  float target = settings.bright_min +
                 frac * (float)(settings.bright_max - settings.bright_min);

  /* Hysteresis stops the panel flickering between two steps when the
   * light level sits on a boundary. */
  if (isnan(auto_level) || fabsf(target - auto_level) > LEVEL_HYSTERESIS)
    auto_level = roundf(target);

  set_brightness_level(auto_level);
}

/* Which view to show right now, including automatic date/temp cycling. */
static view_t current_view(uint32_t ms) {
  if (pinned_view != VIEW_TIME)
    return pinned_view;

  view_t extra[2];
  int n = 0;
  if (settings.cycle_date) extra[n++] = VIEW_DATE;
  if (settings.cycle_temp) extra[n++] = VIEW_TEMPERATURE;
  if (n == 0)
    return VIEW_TIME;

  uint32_t time_ms = (uint32_t)settings.cycle_interval * 1000u;
  uint32_t t = ms % (time_ms + (uint32_t)n * CYCLE_SHOW_MS);
  if (t < time_ms)
    return VIEW_TIME;
  return extra[(t - time_ms) / CYCLE_SHOW_MS];
}

static bool colon_lit(int usec) {
  switch (settings.colon_mode) {
  case COLON_BLINK:
    return usec < 500000;
  case COLON_BLINK_FAST:
    return (usec / 250000) % 2 == 0;
  default:
    return true;
  }
}

static void draw_temperature(void) {
  if (isnan(temperature_c)) {
    /* Sensor open or shorted. */
    hu058_set_text(&display, "--- ");
    hu058_set_char(&display, HU058_M_ONES, settings.fahrenheit ? 'F' : 'C');
    hu058_set_ann(&display, HU058_ANN_DEGREE, true);
    return;
  }

  float t = settings.fahrenheit ? temperature_c * 9.0f / 5.0f + 32.0f
                                : temperature_c;
  t = fminf(fmaxf(t, -99.0f), 999.0f);
  hu058_set_temperature(&display, (int16_t)lroundf(t), settings.fahrenheit);
}

static void render(uint32_t ms) {
  hu058_clear(&display);

  if (ota_active) {
    hu058_set_text(&display, "OtA");
    return;
  }

  /* Display time comes from the RTC (kept set by NTP), or from the system
   * clock if there is no working RTC. */
  time_t utc;
  int usec;
  bool time_valid = clock_time_now(&utc, &usec);
  struct tm tm;
  localtime_r(&utc, &tm);

  view_t view = current_view(ms);

  if (view == VIEW_TEMPERATURE) {
    draw_temperature();
    return;
  }

  if (!time_valid) {
    /* No time yet: say so rather than show a made-up 00:00. Lower case o
     * and d, since a capital O reads as a zero. */
    hu058_set_text(&display, "LoAd");
    return;
  }

  if (view == VIEW_DATE) {
    uint8_t day = (uint8_t)tm.tm_mday, month = (uint8_t)(tm.tm_mon + 1);
    if (settings.date_month_first)
      hu058_set_date(&display, month, day);
    else
      hu058_set_date(&display, day, month);
    return;
  }

  hu058_set_time(&display, (uint8_t)tm.tm_hour, (uint8_t)tm.tm_min);
  if (colon_lit(usec)) {
    /* The upper dot doubles as a network indicator: out while offline. */
    hu058_set_ann(&display, HU058_ANN_COLON_UPPER, wifi_manager_is_connected());
    hu058_set_ann(&display, HU058_ANN_COLON_LOWER, true);
  }
}

/* Two beeps when the local hour changes, if enabled. */
static void hourly_chime(void) {
  static int last_hour = -1;
  time_t utc;
  int usec;
  if (!clock_time_now(&utc, &usec)) return;

  struct tm tm;
  localtime_r(&utc, &tm);
  /* The first valid reading only primes; a time step (NTP correction, DST)
   * is not an hour boundary unless it lands on minute 0. */
  if (last_hour >= 0 && tm.tm_hour != last_hour && tm.tm_min == 0 &&
      settings.hourly_chime && !ota_active)
    PLAY(SOUND_CHIME);
  last_hour = tm.tm_hour;
}

static void lamp_test(void) {
  hu058_set_all_color(&display, HU058_RGB_WHITE);
  hu058_set_brightness(&display, HU058_CURRENT_MAX / 2);
  hu058_set_text(&display, "8888");
  for (int i = 0; i < HU058_NUM_ANN; i++)
    hu058_set_ann(&display, i, true);
  hu058_compile(&display);

  vTaskDelay(pdMS_TO_TICKS(2000));
}

static void show_text(const char *text) {
  hu058_clear(&display);
  apply_colors(now_ms());
  update_brightness();
  hu058_set_text(&display, text);
  hu058_compile(&display);
}

static void wait_for_portal_then_restart(void) {
  while (!captive_portal_settings_received()) {
    if (factory_reset_request) {
      clock_settings_factory_reset();
      esp_restart();
    }
    vTaskDelay(pdMS_TO_TICKS(500));
  }

  captive_portal_stop();
  ESP_LOGI(TAG, "Settings received, restarting...");
  vTaskDelay(pdMS_TO_TICKS(1000));
  esp_restart();
}

static void run_captive_portal_mode(void) {
  show_text("SEt");
  status_led_set(STATUS_LED_PORTAL);

  ESP_LOGI(TAG, "No WiFi configured — starting captive portal");
  captive_portal_start(&settings);
  wait_for_portal_then_restart();
}

void app_main(void) {
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ESP_ERROR_CHECK(nvs_flash_init());
  } else {
    ESP_ERROR_CHECK(ret);
  }

  clock_settings_load(&settings);
  setenv("TZ", settings.tz, 1);
  tzset();

  status_led_init(CONFIG_ESPCLOCK_PIN_STATUS_LED);
  status_led_set(STATUS_LED_BOOT);

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  aip33628_init(&drv_hours, CONFIG_ESPCLOCK_PIN_HOURS_DIN,
                CONFIG_ESPCLOCK_PIN_HOURS_CLK);
  aip33628_init(&drv_minutes, CONFIG_ESPCLOCK_PIN_MINUTES_DIN,
                CONFIG_ESPCLOCK_PIN_MINUTES_CLK);

  hu058_init(&display, &drv_hours, &drv_minutes);
  hu058_set_time_format(&display, settings.time_24h ? HU058_24H : HU058_12H);

  scan_timer_start();

#if CONFIG_ESPCLOCK_BUZZER
  ESP_ERROR_CHECK(buzzer_init(CONFIG_ESPCLOCK_PIN_BUZZER));
#endif

  /* Before WiFi: a set RTC gives the display the time straight away. */
#if CONFIG_ESPCLOCK_RTC
  clock_time_init(true, CONFIG_ESPCLOCK_PIN_RTC_CE, CONFIG_ESPCLOCK_PIN_RTC_SCLK,
                  CONFIG_ESPCLOCK_PIN_RTC_IO);
#else
  clock_time_init(false, -1, -1, -1);
#endif

  buttons_hal_init(CONFIG_ESPCLOCK_PIN_BUTTON1, CONFIG_ESPCLOCK_PIN_BUTTON2,
                   button_handler);

  ESP_ERROR_CHECK(clock_sensors_init(CONFIG_ESPCLOCK_PIN_NTC_ADC,
                                     CONFIG_ESPCLOCK_PIN_LDR_ADC,
                                     CONFIG_ESPCLOCK_PIN_NTC_POWER));
  update_sensors();
  ESP_LOGI(TAG, "LDR: %.0f ohm, temperature: %.1f C",
           clock_sensors_ldr_ohms(), temperature_c);

  lamp_test();
  PLAY(SOUND_BOOT);
#if CONFIG_ESPCLOCK_BUZZER_TEST
  ESP_LOGW(TAG, "Buzzer test: continuous %d Hz on GPIO%d", BUZZER_DEFAULT_HZ,
           CONFIG_ESPCLOCK_PIN_BUZZER);
#endif

  if (!clock_settings_has_wifi(&settings)) {
    run_captive_portal_mode();
    return;
  }

  ESP_LOGI(TAG, "Connecting to WiFi '%s'...", settings.wifi_ssid);
  status_led_set(STATUS_LED_WIFI_CONNECTING);
  wifi_manager_init();

  /* Don't wait for the connection: the display runs from the RTC meanwhile,
   * and NTP and OTA pick up whenever the network does. */
  wifi_manager_connect(settings.wifi_ssid, settings.wifi_pass, 0);

  clock_ntp_start(settings.ntp_server);

  /* Settings page and firmware upload at the clock's address. */
  web_server_start(&settings, web_settings_saved, ota_status);

  /* Reaching the main loop is good enough to keep a freshly updated image;
   * if it crashed before here the bootloader rolls back on the next boot. */
  esp_ota_mark_app_valid_cancel_rollback();

  /* Yellow while the first connection is in progress, then green/red. */
  const uint32_t connect_grace_ms = now_ms() + 15000;
  bool ever_connected = false;
  status_led_state_t led_state = STATUS_LED_WIFI_CONNECTING;
  time_t last_second = 0;
  uint32_t last_sensor_ms = 0;
#if CONFIG_ESPCLOCK_BUZZER_TEST
  uint32_t last_test_ms = 0;
#endif

  while (1) {
    if (factory_reset_request) {
      ESP_LOGW(TAG, "Factory reset requested");
      clock_settings_factory_reset();
      esp_restart();
    }

    if (enter_portal_request) {
      enter_portal_request = false;
      ESP_LOGI(TAG, "Entering config portal");
      show_text("SEt");
      status_led_set(STATUS_LED_PORTAL);
      web_server_stop();
      wifi_manager_disconnect();
      captive_portal_start(&settings);
      wait_for_portal_then_restart();
    }

    uint32_t ms = now_ms();

    bool connected = wifi_manager_is_connected();
    ever_connected |= connected;
    status_led_state_t want =
        connected ? STATUS_LED_RUNNING
        : (!ever_connected && (int32_t)(ms - connect_grace_ms) < 0)
            ? STATUS_LED_WIFI_CONNECTING
            : STATUS_LED_WIFI_FAILED;
    if (want != led_state) {
      led_state = want;
      status_led_set(want);
    }

    apply_pending_settings();
    clock_time_service();
    hourly_chime();

    time_t now = time(NULL);
    if (now != last_second) {
      last_second = now;
      /* Heartbeat on the status LED so a hung loop is visible. */
      status_led_pulse();
    }

    if (ms - last_sensor_ms >= SENSOR_PERIOD_MS) {
      last_sensor_ms = ms;
      update_sensors();
      publish_status();
    }

#if CONFIG_ESPCLOCK_BUZZER_TEST
    /* Each beep outlasts the refresh, so the tone runs on with only a
     * momentary restart; a key click cuts it short until the next refresh. */
    if (ms - last_test_ms >= 10000) {
      last_test_ms = ms;
      buzzer_beep(BUZZER_DEFAULT_HZ, 15000);
    }
#endif

    update_brightness();
    apply_colors(ms);
    render(ms);
    hu058_compile(&display);

    vTaskDelay(pdMS_TO_TICKS(TICK_MS));
  }
}
