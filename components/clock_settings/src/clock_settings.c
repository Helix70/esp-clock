#include "clock_settings.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "settings";
static const char *NVS_NAMESPACE = "clock_cfg";

static void apply_defaults(clock_settings_t *s)
{
    if (s->ntp_server[0] == '\0')
        snprintf(s->ntp_server, sizeof(s->ntp_server), "pool.ntp.org");

    if (s->tz[0] == '\0')
        snprintf(s->tz, sizeof(s->tz), "UTC0");

    if (s->brightness == 0)
        s->brightness = 3;

    if (!s->led_r && !s->led_g && !s->led_b) {
        s->led_r = true;
        s->led_g = true;
        s->led_b = true;
    }
}

int clock_settings_load(clock_settings_t *s)
{
    memset(s, 0, sizeof(*s));

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No saved settings, using defaults");
        apply_defaults(s);
        return 0;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
        apply_defaults(s);
        return err;
    }

    size_t len;

    len = sizeof(s->wifi_ssid);
    nvs_get_str(h, "wifi_ssid", s->wifi_ssid, &len);

    len = sizeof(s->wifi_pass);
    nvs_get_str(h, "wifi_pass", s->wifi_pass, &len);

    uint8_t u8;
    if (nvs_get_u8(h, "fahrenheit", &u8) == ESP_OK) s->fahrenheit = u8;
    if (nvs_get_u8(h, "time_24h", &u8) == ESP_OK) s->time_24h = u8;
    if (nvs_get_u8(h, "blink_sep", &u8) == ESP_OK) s->blink_separator = u8;

    len = sizeof(s->ntp_server);
    nvs_get_str(h, "ntp_server", s->ntp_server, &len);

    len = sizeof(s->tz);
    nvs_get_str(h, "tz", s->tz, &len);

    /* Migrate the old fixed UTC offset (seconds) to a POSIX TZ string.
     * POSIX sign is inverted: UTC+10 is written "UTC-10". */
    int32_t i32;
    if (s->tz[0] == '\0' && nvs_get_i32(h, "utc_offset", &i32) == ESP_OK)
        snprintf(s->tz, sizeof(s->tz), "UTC%d", (int)(-i32 / 3600));

    if (nvs_get_u8(h, "brightness", &u8) == ESP_OK) s->brightness = u8;
    if (nvs_get_u8(h, "led_r", &u8) == ESP_OK) s->led_r = u8;
    if (nvs_get_u8(h, "led_g", &u8) == ESP_OK) s->led_g = u8;
    if (nvs_get_u8(h, "led_b", &u8) == ESP_OK) s->led_b = u8;

    int8_t i8;
    if (nvs_get_i8(h, "temp_off", &i8) == ESP_OK) s->temp_offset_tenths = i8;

    nvs_close(h);

    apply_defaults(s);

    ESP_LOGI(TAG, "Settings loaded (SSID: %s, TZ: %s)", s->wifi_ssid, s->tz);
    return 0;
}

int clock_settings_save(const clock_settings_t *s)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open for write failed: %s", esp_err_to_name(err));
        return err;
    }

    nvs_set_str(h, "wifi_ssid", s->wifi_ssid);
    nvs_set_str(h, "wifi_pass", s->wifi_pass);
    nvs_set_u8(h, "fahrenheit", s->fahrenheit ? 1 : 0);
    nvs_set_u8(h, "time_24h", s->time_24h ? 1 : 0);
    nvs_set_u8(h, "blink_sep", s->blink_separator ? 1 : 0);
    nvs_set_str(h, "ntp_server", s->ntp_server);
    nvs_set_str(h, "tz", s->tz);
    nvs_erase_key(h, "utc_offset"); /* legacy key, migrated on load */
    nvs_set_u8(h, "brightness", s->brightness);
    nvs_set_u8(h, "led_r", s->led_r ? 1 : 0);
    nvs_set_u8(h, "led_g", s->led_g ? 1 : 0);
    nvs_set_u8(h, "led_b", s->led_b ? 1 : 0);
    nvs_set_i8(h, "temp_off", s->temp_offset_tenths);

    err = nvs_commit(h);
    nvs_close(h);

    if (err == ESP_OK)
        ESP_LOGI(TAG, "Settings saved");
    else
        ESP_LOGE(TAG, "NVS commit failed: %s", esp_err_to_name(err));

    return err;
}

void clock_settings_factory_reset(void)
{
    ESP_LOGW(TAG, "Factory reset — erasing NVS namespace");
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
    }
}

bool clock_settings_has_wifi(const clock_settings_t *s)
{
    return s->wifi_ssid[0] != '\0';
}
