#ifndef CLOCK_SETTINGS_H
#define CLOCK_SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

#define CLOCK_SETTINGS_SSID_MAX     32
#define CLOCK_SETTINGS_PASS_MAX     64
#define CLOCK_SETTINGS_NTP_MAX      64
#define CLOCK_SETTINGS_TZ_MAX       48
#define CLOCK_SETTINGS_OTA_PASS_MAX 32
#define CLOCK_SETTINGS_NUM_COLORS   4

/** Brightness levels run 1..CLOCK_BRIGHTNESS_MAX. */
#define CLOCK_BRIGHTNESS_MAX        16

/** Temperature calibration limit, tenths of a degree either way. */
#define CLOCK_TEMP_OFFSET_MAX       150

typedef enum {
    COLON_STEADY = 0,
    COLON_BLINK = 1,      /* on for half of each second */
    COLON_BLINK_FAST = 2, /* twice a second */
} colon_mode_t;

typedef enum {
    COLOR_MODE_SOLID = 0,        /* colors[0] everywhere */
    COLOR_MODE_GRADIENT = 1,     /* colors[0] on the left to colors[1] on the right */
    COLOR_MODE_PER_DIGIT = 2,    /* colors[0..3], one per digit */
    COLOR_MODE_RAINBOW = 3,      /* whole panel cycles through the colour wheel */
    COLOR_MODE_RAINBOW_WAVE = 4, /* colour wheel sweeps across the panel */
    COLOR_MODE_COUNT
} color_mode_t;

typedef struct {
    char wifi_ssid[CLOCK_SETTINGS_SSID_MAX + 1];
    char wifi_pass[CLOCK_SETTINGS_PASS_MAX + 1];
    char ntp_server[CLOCK_SETTINGS_NTP_MAX + 1];
    char tz[CLOCK_SETTINGS_TZ_MAX + 1]; /* POSIX TZ string, e.g. "CET-1CEST,M3.5.0,M10.5.0/3" */

    bool time_24h;
    uint8_t colon_mode;       /* colon_mode_t */

    uint8_t brightness;       /* 1..CLOCK_BRIGHTNESS_MAX, used when auto is off */
    bool auto_brightness;
    uint8_t bright_min;       /* auto brightness in the dark */
    uint8_t bright_max;       /* auto brightness in full light */

    uint8_t color_mode;       /* color_mode_t */
    uint32_t colors[CLOCK_SETTINGS_NUM_COLORS]; /* 0xRRGGBB */
    uint16_t effect_speed;    /* seconds per rainbow cycle */

    bool cycle_date;          /* show the date between the time */
    bool cycle_temp;          /* show the temperature between the time */
    uint8_t cycle_interval;   /* seconds of time between cycles */
    bool date_month_first;    /* MM-DD rather than DD-MM */

    bool fahrenheit;
    int16_t temp_offset_tenths; /* added to the reading, -150..150 */

    bool key_beep;            /* beep on button presses */
    bool hourly_chime;        /* two short beeps on the hour */

    char ota_password[CLOCK_SETTINGS_OTA_PASS_MAX + 1]; /* empty disables OTA */
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
 * @brief Clamp every field into its valid range.
 */
void clock_settings_sanitize(clock_settings_t *s);

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
