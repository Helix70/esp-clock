#include "aip33628.h"
#include "buttons_hal.h"
#include "captive_portal.h"
#include "clock_ntp.h"
#include "clock_settings.h"
#include "hu058.h"
#include "status_led.h"
#include "wifi_manager.h"

#include "driver/gptimer.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *TAG = "clock";

static aip33628_t drv_hours;
static aip33628_t drv_minutes;
static hu058_display_t display;
static clock_settings_t settings;
static adc_oneshot_unit_handle_t adc_handle;
static adc_channel_t ntc_channel;
static adc_channel_t ldr_channel;
static gptimer_handle_t scan_timer;

#define SCAN_PERIOD_US 625

#define NTC_BETA 3950.0f
#define NTC_R0 10000.0f
#define NTC_T0 298.15f
#define NTC_R_PULLUP 10000.0f

typedef enum {
  MODE_TIME,
  MODE_TEMPERATURE,
} display_mode_t;

static volatile display_mode_t current_mode = MODE_TIME;
static volatile bool enter_portal_request = false;
static volatile bool factory_reset_request = false;

static const uint8_t BRIGHTNESS_MAP[] = {
    HU058_BRIGHTNESS_1, HU058_BRIGHTNESS_2, HU058_BRIGHTNESS_3,
    HU058_BRIGHTNESS_4, HU058_BRIGHTNESS_5,
};

/*
 * Display scan runs on its own GPTimer rather than esp_timer. With an
 * ISR-dispatched esp_timer at 625 us, every task-dispatched alarm (20 ms
 * button poll, 500 ms LED blink: all multiples of 625 us) lands on the same
 * alarm tick, esp_timer's ISR reports "ISR timers processed" and never
 * notifies its task, so task-dispatched callbacks starve forever.
 */
static bool IRAM_ATTR scan_timer_cb(gptimer_handle_t timer,
                                    const gptimer_alarm_event_data_t *edata,
                                    void *arg) {
  (void)timer;
  (void)edata;
  (void)arg;
  hu058_scan_step(&display);
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
      .alarm_count = SCAN_PERIOD_US,
      .reload_count = 0,
      .flags.auto_reload_on_alarm = true,
  };
  ESP_ERROR_CHECK(gptimer_set_alarm_action(scan_timer, &alarm));
  ESP_ERROR_CHECK(gptimer_enable(scan_timer));
  ESP_ERROR_CHECK(gptimer_start(scan_timer));
}

static void button_handler(button_event_t event) {
  switch (event) {
  case BUTTON_1_PRESS:
    current_mode = (current_mode == MODE_TIME) ? MODE_TEMPERATURE : MODE_TIME;
    break;
  case BUTTON_1_HELD:
    enter_portal_request = true;
    break;
  case BUTTON_BOTH_HELD:
    factory_reset_request = true;
    break;
  default:
    break;
  }
}

static hu058_color_t color_from_settings(const clock_settings_t *s) {
  return (hu058_color_t){.r = s->led_r, .g = s->led_g, .b = s->led_b};
}

static void apply_display_settings(void) {
  uint8_t idx = settings.brightness;
  if (idx < 1)
    idx = 1;
  if (idx > 5)
    idx = 5;
  hu058_set_brightness(&display, BRIGHTNESS_MAP[idx - 1]);

  hu058_color_t color = color_from_settings(&settings);
  for (int i = 0; i < HU058_NUM_DIGITS; i++)
    hu058_set_digit_color(&display, i, color);
  for (int i = 0; i < HU058_NUM_ANN; i++)
    hu058_set_ann_color(&display, i, color);

  hu058_set_time_format(&display, settings.time_24h ? HU058_24H : HU058_12H);
}

static void lamp_test(void) {
  for (int i = 0; i < HU058_NUM_DIGITS; i++)
    hu058_set_digit(&display, i, 8);
  for (int i = 0; i < HU058_NUM_ANN; i++)
    hu058_set_ann(&display, i, true);
  hu058_compile(&display);

  vTaskDelay(pdMS_TO_TICKS(3000));

  for (int i = 0; i < HU058_NUM_ANN; i++)
    hu058_set_ann(&display, i, false);
}

static void show_boot_text(void) {
  hu058_set_segments(&display, HU058_H_TENS,
                     HU058_SEG_C | HU058_SEG_D | HU058_SEG_E | HU058_SEG_F |
                         HU058_SEG_G);
  hu058_set_segments(&display, HU058_H_ONES,
                     HU058_SEG_C | HU058_SEG_D | HU058_SEG_E | HU058_SEG_G);
  hu058_set_segments(&display, HU058_M_TENS,
                     HU058_SEG_C | HU058_SEG_D | HU058_SEG_E | HU058_SEG_G);
  hu058_set_segments(&display, HU058_M_ONES,
                     HU058_SEG_D | HU058_SEG_E | HU058_SEG_F | HU058_SEG_G);
  hu058_compile(&display);
}

static void run_captive_portal_mode(void) {
  show_boot_text();
  status_led_set(STATUS_LED_PORTAL);

  ESP_LOGI(TAG, "No WiFi configured — starting captive portal");
  captive_portal_start(&settings);

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

static float read_temperature(void) {
  int raw;
  adc_oneshot_read(adc_handle, ntc_channel, &raw);

  if (raw <= 0)
    raw = 1;
  float r_ntc = NTC_R_PULLUP * (float)raw / (4095.0f - (float)raw);
  float t_kelvin = 1.0f / (1.0f / NTC_T0 + logf(r_ntc / NTC_R0) / NTC_BETA);
  float temp_c = t_kelvin - 273.15f;

  return temp_c + (float)settings.temp_offset_tenths / 10.0f;
}

/* Higher raw value = darker (LDR resistance high, divider voltage high). */
static int read_light_raw(void) {
  int raw = 0;
  adc_oneshot_read(adc_handle, ldr_channel, &raw);
  return raw;
}

static void adc_init(void) {
  adc_unit_t ntc_unit, ldr_unit;
  ESP_ERROR_CHECK(adc_oneshot_io_to_channel(CONFIG_ESPCLOCK_PIN_NTC_ADC,
                                            &ntc_unit, &ntc_channel));
  ESP_ERROR_CHECK(adc_oneshot_io_to_channel(CONFIG_ESPCLOCK_PIN_LDR_ADC,
                                            &ldr_unit, &ldr_channel));
  assert(ntc_unit == ldr_unit);

  adc_oneshot_unit_init_cfg_t adc_cfg = {.unit_id = ntc_unit};
  ESP_ERROR_CHECK(adc_oneshot_new_unit(&adc_cfg, &adc_handle));
  adc_oneshot_chan_cfg_t chan_cfg = {
      .atten = ADC_ATTEN_DB_12,
      .bitwidth = ADC_BITWIDTH_DEFAULT,
  };
  ESP_ERROR_CHECK(
      adc_oneshot_config_channel(adc_handle, ntc_channel, &chan_cfg));
  ESP_ERROR_CHECK(
      adc_oneshot_config_channel(adc_handle, ldr_channel, &chan_cfg));
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
  apply_display_settings();

  scan_timer_start();

  buttons_hal_init(CONFIG_ESPCLOCK_PIN_BUTTON1, CONFIG_ESPCLOCK_PIN_BUTTON2,
                   button_handler);

  adc_init();
  ESP_LOGI(TAG, "LDR raw: %d, temperature: %.1f C", read_light_raw(),
           read_temperature());

  lamp_test();

  if (!clock_settings_has_wifi(&settings)) {
    run_captive_portal_mode();
    return;
  }

  ESP_LOGI(TAG, "Connecting to WiFi '%s'...", settings.wifi_ssid);
  status_led_set(STATUS_LED_WIFI_CONNECTING);
  wifi_manager_init();

  if (wifi_manager_connect(settings.wifi_ssid, settings.wifi_pass, 15000) ==
      ESP_OK) {
    ESP_LOGI(TAG, "WiFi connected, starting NTP");
    clock_ntp_start(settings.ntp_server);
    status_led_set(STATUS_LED_RUNNING);
  } else {
    ESP_LOGW(TAG, "WiFi connection failed — running without NTP");
    status_led_set(STATUS_LED_WIFI_FAILED);
  }

  bool colon_on = true;
  time_t last_second = 0;

  while (1) {
    if (factory_reset_request) {
      ESP_LOGW(TAG, "Factory reset requested");
      clock_settings_factory_reset();
      esp_restart();
    }

    if (enter_portal_request) {
      enter_portal_request = false;
      ESP_LOGI(TAG, "Entering config portal");
      status_led_set(STATUS_LED_PORTAL);
      wifi_manager_disconnect();
      captive_portal_start(&settings);
      while (!captive_portal_settings_received()) {
        if (factory_reset_request) {
          clock_settings_factory_reset();
          esp_restart();
        }
        vTaskDelay(pdMS_TO_TICKS(500));
      }
      captive_portal_stop();
      ESP_LOGI(TAG, "Settings updated, restarting...");
      vTaskDelay(pdMS_TO_TICKS(1000));
      esp_restart();
    }

    time_t now = time(NULL);
    struct tm local_tm;
    localtime_r(&now, &local_tm);

    if (now != last_second) {
      last_second = now;

      /* Heartbeat on the status LED so a hung loop is visible. */
      status_led_pulse();

      if (settings.blink_separator) {
        colon_on = !colon_on;
      } else {
        colon_on = true;
      }

      switch (current_mode) {
      case MODE_TIME:
        hu058_set_time(&display, local_tm.tm_hour, local_tm.tm_min);
        hu058_set_ann(&display, HU058_ANN_COLON_UPPER, colon_on);
        hu058_set_ann(&display, HU058_ANN_COLON_LOWER, colon_on);
        hu058_set_ann(&display, HU058_ANN_DEGREE, false);
        hu058_set_ann(&display, HU058_ANN_DASH, false);
        break;

      case MODE_TEMPERATURE: {
        float temp_c = read_temperature();
        int16_t temp_val;
        if (settings.fahrenheit) {
          temp_val = (int16_t)(temp_c * 9.0f / 5.0f + 32.0f + 0.5f);
        } else {
          temp_val = (int16_t)(temp_c + (temp_c >= 0 ? 0.5f : -0.5f));
        }
        hu058_set_temperature(&display, temp_val, settings.fahrenheit);
        hu058_set_ann(&display, HU058_ANN_COLON_UPPER, false);
        hu058_set_ann(&display, HU058_ANN_COLON_LOWER, false);
        hu058_set_ann(&display, HU058_ANN_DASH, false);
        break;
      }
      }

      hu058_compile(&display);
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
