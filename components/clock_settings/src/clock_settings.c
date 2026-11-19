#include "clock_settings.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "settings";
static const char *NVS_NAMESPACE = "clock_cfg";

static const uint32_t DEFAULT_COLORS[CLOCK_SETTINGS_NUM_COLORS] = {
    0xFFFFFF, 0x0080FF, 0x00FF80, 0xFF8000,
};

static void apply_defaults(clock_settings_t *s)
{
    snprintf(s->ntp_server, sizeof(s->ntp_server), "pool.ntp.org");
    snprintf(s->tz, sizeof(s->tz), "UTC0");
    s->time_24h = false;
    s->colon_mode = COLON_STEADY;
    s->brightness = 11;
    s->auto_brightness = false;
    s->bright_min = 1;
    s->bright_max = CLOCK_BRIGHTNESS_MAX;
    s->color_mode = COLOR_MODE_SOLID;
    memcpy(s->colors, DEFAULT_COLORS, sizeof(s->colors));
    s->effect_speed = 30;
    s->cycle_date = false;
    s->cycle_temp = false;
    s->cycle_interval = 30;
    s->date_month_first = false;
    s->key_beep = true;
    s->hourly_chime = false;
}

static uint8_t clamp_u8(uint8_t v, uint8_t lo, uint8_t hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

void clock_settings_sanitize(clock_settings_t *s)
{
    if (s->ntp_server[0] == '\0')
        snprintf(s->ntp_server, sizeof(s->ntp_server), "pool.ntp.org");
    if (s->tz[0] == '\0')
        snprintf(s->tz, sizeof(s->tz), "UTC0");
    if (s->colon_mode > COLON_BLINK_FAST)
        s->colon_mode = COLON_STEADY;
    s->brightness = clamp_u8(s->brightness, 1, CLOCK_BRIGHTNESS_MAX);
    s->bright_min = clamp_u8(s->bright_min, 1, CLOCK_BRIGHTNESS_MAX);
    s->bright_max = clamp_u8(s->bright_max, 1, CLOCK_BRIGHTNESS_MAX);
    if (s->bright_min > s->bright_max)
        s->bright_min = s->bright_max;
    if (s->color_mode >= COLOR_MODE_COUNT)
        s->color_mode = COLOR_MODE_SOLID;
    for (int i = 0; i < CLOCK_SETTINGS_NUM_COLORS; i++)
        s->colors[i] &= 0xFFFFFF;
    if (s->effect_speed < 1) s->effect_speed = 1;
    if (s->effect_speed > 600) s->effect_speed = 600;
    s->cycle_interval = clamp_u8(s->cycle_interval, 5, 240);
    if (s->temp_offset_tenths < -CLOCK_TEMP_OFFSET_MAX) s->temp_offset_tenths = -CLOCK_TEMP_OFFSET_MAX;
    if (s->temp_offset_tenths > CLOCK_TEMP_OFFSET_MAX) s->temp_offset_tenths = CLOCK_TEMP_OFFSET_MAX;
}

/* Settings saved by firmware before colour and 16-step brightness. */
static void migrate_legacy(nvs_handle_t h, clock_settings_t *s)
{
    uint8_t u8;
    int32_t i32;

    /* Fixed UTC offset (seconds) to a POSIX TZ string.
     * POSIX sign is inverted: UTC+10 is written "UTC-10". */
    if (s->tz[0] == '\0' && nvs_get_i32(h, "utc_offset", &i32) == ESP_OK)
        snprintf(s->tz, sizeof(s->tz), "UTC%d", (int)(-i32 / 3600));

    if (nvs_get_u8(h, "blink_sep", &u8) == ESP_OK)
        s->colon_mode = u8 ? COLON_BLINK : COLON_STEADY;

    /* Brightness 1-5 to the nearest of 16 levels. */
    static const uint8_t BRIGHT5_TO_16[] = {1, 4, 8, 12, 16};
    if (nvs_get_u8(h, "brightness", &u8) == ESP_OK && u8 >= 1 && u8 <= 5)
        s->brightness = BRIGHT5_TO_16[u8 - 1];

    /* Per-channel on/off to a solid colour. */
    uint8_t r = 1, g = 1, b = 1;
    bool any = false;
    if (nvs_get_u8(h, "led_r", &u8) == ESP_OK) { r = u8; any = true; }
    if (nvs_get_u8(h, "led_g", &u8) == ESP_OK) { g = u8; any = true; }
    if (nvs_get_u8(h, "led_b", &u8) == ESP_OK) { b = u8; any = true; }
    if (any && (r || g || b))
        s->colors[0] = (r ? 0xFF0000 : 0) | (g ? 0x00FF00 : 0) | (b ? 0x0000FF : 0);
}

int clock_settings_load(clock_settings_t *s)
{
    memset(s, 0, sizeof(*s));
    apply_defaults(s);

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No saved settings, using defaults");
        return 0;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return err;
    }

    size_t len;
    uint8_t u8;
    uint16_t u16;
    uint32_t u32;
    int8_t i8;

    len = sizeof(s->wifi_ssid);
    nvs_get_str(h, "wifi_ssid", s->wifi_ssid, &len);
    len = sizeof(s->wifi_pass);
    nvs_get_str(h, "wifi_pass", s->wifi_pass, &len);
    len = sizeof(s->ntp_server);
    nvs_get_str(h, "ntp_server", s->ntp_server, &len);
    s->tz[0] = '\0';
    len = sizeof(s->tz);
    nvs_get_str(h, "tz", s->tz, &len);

    /* Old keys first, so anything saved in the new format wins. */
    if (nvs_get_u8(h, "version", &u8) != ESP_OK)
        migrate_legacy(h, s);

    if (nvs_get_u8(h, "time_24h", &u8) == ESP_OK) s->time_24h = u8;
    if (nvs_get_u8(h, "colon", &u8) == ESP_OK) s->colon_mode = u8;
    if (nvs_get_u8(h, "bright16", &u8) == ESP_OK) s->brightness = u8;
    if (nvs_get_u8(h, "auto_br", &u8) == ESP_OK) s->auto_brightness = u8;
    if (nvs_get_u8(h, "br_min", &u8) == ESP_OK) s->bright_min = u8;
    if (nvs_get_u8(h, "br_max", &u8) == ESP_OK) s->bright_max = u8;
    if (nvs_get_u8(h, "color_mode", &u8) == ESP_OK) s->color_mode = u8;
    for (int i = 0; i < CLOCK_SETTINGS_NUM_COLORS; i++) {
        char key[8];
        snprintf(key, sizeof(key), "color%d", i);
        if (nvs_get_u32(h, key, &u32) == ESP_OK) s->colors[i] = u32;
    }
    if (nvs_get_u16(h, "fx_speed", &u16) == ESP_OK) s->effect_speed = u16;
    if (nvs_get_u8(h, "cyc_date", &u8) == ESP_OK) s->cycle_date = u8;
    if (nvs_get_u8(h, "cyc_temp", &u8) == ESP_OK) s->cycle_temp = u8;
    if (nvs_get_u8(h, "cyc_int", &u8) == ESP_OK) s->cycle_interval = u8;
    if (nvs_get_u8(h, "date_mf", &u8) == ESP_OK) s->date_month_first = u8;
    if (nvs_get_u8(h, "fahrenheit", &u8) == ESP_OK) s->fahrenheit = u8;
    /* temp_off held the offset as int8 when it was limited to +-5.0. */
    int16_t i16;
    if (nvs_get_i16(h, "temp_off16", &i16) == ESP_OK) s->temp_offset_tenths = i16;
    else if (nvs_get_i8(h, "temp_off", &i8) == ESP_OK) s->temp_offset_tenths = i8;
    if (nvs_get_u8(h, "key_beep", &u8) == ESP_OK) s->key_beep = u8;
    if (nvs_get_u8(h, "chime", &u8) == ESP_OK) s->hourly_chime = u8;
    len = sizeof(s->ota_password);
    nvs_get_str(h, "ota_pass", s->ota_password, &len);

    nvs_close(h);

    clock_settings_sanitize(s);
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

    nvs_set_u8(h, "version", 2);
    nvs_set_str(h, "wifi_ssid", s->wifi_ssid);
    nvs_set_str(h, "wifi_pass", s->wifi_pass);
    nvs_set_str(h, "ntp_server", s->ntp_server);
    nvs_set_str(h, "tz", s->tz);
    nvs_set_u8(h, "time_24h", s->time_24h ? 1 : 0);
    nvs_set_u8(h, "colon", s->colon_mode);
    nvs_set_u8(h, "bright16", s->brightness);
    nvs_set_u8(h, "auto_br", s->auto_brightness ? 1 : 0);
    nvs_set_u8(h, "br_min", s->bright_min);
    nvs_set_u8(h, "br_max", s->bright_max);
    nvs_set_u8(h, "color_mode", s->color_mode);
    for (int i = 0; i < CLOCK_SETTINGS_NUM_COLORS; i++) {
        char key[8];
        snprintf(key, sizeof(key), "color%d", i);
        nvs_set_u32(h, key, s->colors[i]);
    }
    nvs_set_u16(h, "fx_speed", s->effect_speed);
    nvs_set_u8(h, "cyc_date", s->cycle_date ? 1 : 0);
    nvs_set_u8(h, "cyc_temp", s->cycle_temp ? 1 : 0);
    nvs_set_u8(h, "cyc_int", s->cycle_interval);
    nvs_set_u8(h, "date_mf", s->date_month_first ? 1 : 0);
    nvs_set_u8(h, "fahrenheit", s->fahrenheit ? 1 : 0);
    nvs_set_i16(h, "temp_off16", s->temp_offset_tenths);
    nvs_set_u8(h, "key_beep", s->key_beep ? 1 : 0);
    nvs_set_u8(h, "chime", s->hourly_chime ? 1 : 0);
    nvs_set_str(h, "ota_pass", s->ota_password);

    /* Legacy keys, migrated on load. */
    nvs_erase_key(h, "utc_offset");
    nvs_erase_key(h, "blink_sep");
    nvs_erase_key(h, "brightness");
    nvs_erase_key(h, "led_r");
    nvs_erase_key(h, "led_g");
    nvs_erase_key(h, "led_b");

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
