#ifndef SETTINGS_FORM_H
#define SETTINGS_FORM_H

#include "clock_settings.h"
#include "esp_http_server.h"
#include <stdbool.h>
#include <stddef.h>

/**
 * @file settings_form.h
 * @brief The settings web page and its form handling, shared by the setup
 *        portal and the page served on the home network.
 */

typedef enum {
    /** Setup portal on the clock's own AP: every setting, including the
     *  WiFi credentials and the OTA password. Saving restarts the clock. */
    SETTINGS_FORM_SETUP,
    /** Home network page: everything except the WiFi credentials and the
     *  OTA password, plus the firmware upload. Saving applies at once. */
    SETTINGS_FORM_NETWORK,
} settings_form_mode_t;

typedef struct {
    const char *label;
    const char *posix;
} settings_form_tz_t;

/** Timezones offered on the page, as POSIX TZ strings. */
const settings_form_tz_t *settings_form_tz_table(size_t *count);

/**
 * @brief Build the page for the given settings.
 * @param ota_enabled NETWORK mode only: show the firmware upload (an OTA
 *                    password is set) or explain how to turn it on.
 * @return malloc'd page for free(), or NULL when out of memory.
 */
char *settings_form_page(const clock_settings_t *s, settings_form_mode_t mode,
                         bool ota_enabled);

/**
 * @brief Read a whole urlencoded form body.
 * @return malloc'd, NUL-terminated body for free(), or NULL after sending
 *         an error response.
 */
char *settings_form_recv(httpd_req_t *req);

/**
 * @brief Update settings from a submitted form. NETWORK mode leaves the
 *        WiFi credentials and the OTA password alone whatever is posted.
 *        Call clock_settings_sanitize() afterwards.
 */
void settings_form_parse(const char *body, clock_settings_t *s,
                         settings_form_mode_t mode);

#endif
