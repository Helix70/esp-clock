#ifndef CLOCK_SETTINGS_H
#define CLOCK_SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

#define CLOCK_SETTINGS_SSID_MAX     32
#define CLOCK_SETTINGS_PASS_MAX     64
#define CLOCK_SETTINGS_NTP_MAX      64
#define CLOCK_SETTINGS_TZ_MAX       48

typedef struct {
    char wifi_ssid[CLOCK_SETTINGS_SSID_MAX + 1];
    char wifi_pass[CLOCK_SETTINGS_PASS_MAX + 1];
    bool fahrenheit;
    bool time_24h;
    bool blink_separator;
    char ntp_server[CLOCK_SETTINGS_NTP_MAX + 1];
    char tz[CLOCK_SETTINGS_TZ_MAX + 1]; /* POSIX TZ string, e.g. "CET-1CEST,M3.5.0,M10.5.0/3" */
    uint8_t brightness;
    bool led_r;
    bool led_g;
    bool led_b;
    int8_t temp_offset_tenths;
} clock_settings_t;

/**
 * @brief Load settings from NVS, applying defaults for missing keys.
 * @param s  Settings struct to populate.
 * @return ESP_OK on success.
 */
int clock_settings_load(clock_settings_t *s);

/**
 * @brief Persist all settings to NVS.
 * @param s  Settings struct to save.
 * @return ESP_OK on success.
 */
int clock_settings_save(const clock_settings_t *s);

/**
 * @brief Erase all settings from NVS (factory reset).
 *
 * After calling this, restart the device. On next boot,
 * clock_settings_load() will return defaults.
 */
void clock_settings_factory_reset(void);

/**
 * @brief Check whether WiFi credentials are configured.
 * @param s  Settings to check.
 * @return true if wifi_ssid is non-empty.
 */
bool clock_settings_has_wifi(const clock_settings_t *s);

#endif
