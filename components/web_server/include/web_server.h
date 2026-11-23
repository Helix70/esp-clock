#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "clock_settings.h"
#include "esp_err.h"
#include "ota_server.h"

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/**
 * @file web_server.h
 * @brief Settings page and firmware upload on the home network.
 *
 * Serves the settings page on port 80 at the clock's IP address. Every
 * setting except the WiFi credentials and the OTA password can be changed
 * there; those stay in the setup portal, so nobody on the network can lock
 * the clock out or take over its updates. Saved settings go to NVS and
 * apply without a restart. With an OTA password set, the page also takes
 * firmware uploads (see ota_server.h).
 */

/** Called on the server task after settings are saved. */
typedef void (*web_server_settings_cb_t)(const clock_settings_t *settings);

/**
 * @param settings    Current settings; copied, so the caller's may change.
 * @param settings_cb Called with the new settings after each save.
 * @param ota_cb      Optional firmware upload status callback.
 */
esp_err_t web_server_start(const clock_settings_t *settings,
                           web_server_settings_cb_t settings_cb,
                           ota_server_status_cb_t ota_cb);

void web_server_stop(void);

/** What the clock is showing, for the live readout on the page. */
typedef struct {
    bool time_valid;      /* false until the RTC or NTP has the time */
    time_t utc;           /* display time when captured */
    int32_t usec;
    int32_t utc_offset_s; /* local time minus UTC at that moment */
    int64_t captured_us;  /* esp_timer_get_time() when captured */
    float temp_c;         /* calibrated reading, NAN if the sensor fails */
    const char *source;   /* "rtc", "ntp" or "none"; must be a literal */
    /* Sensor debug: averaged ADC codes (-1 = failed) and calibrated mV
     * (-1 = uncalibrated), LDR ohms (NAN if pinned at a rail), smoothed
     * light fraction (NAN until first reading), brightness level applied. */
    int ntc_raw, ntc_mv;
    int ldr_raw, ldr_mv;
    float ldr_ohms;
    float light;
    int level;
} web_server_status_t;

/**
 * @brief Publish the clock's current time and temperature. Call from the
 *        task that owns the clock (clock_time is single-task) every few
 *        hundred ms; GET /status serves the latest copy, advanced to the
 *        moment of the request. Safe to call before web_server_start().
 */
void web_server_publish_status(const web_server_status_t *status);

#endif
