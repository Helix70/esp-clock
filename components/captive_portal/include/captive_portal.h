#ifndef CAPTIVE_PORTAL_H
#define CAPTIVE_PORTAL_H

#include "clock_settings.h"

/**
 * @brief Start the captive portal (soft AP + DNS redirect + HTTP config).
 *
 * Opens a WiFi soft AP named "ESP-Clock-XXXX" (last 4 hex digits of MAC).
 * All DNS queries resolve to 192.168.4.1. An HTTP server serves a
 * configuration page where the user can set WiFi credentials and other
 * settings.
 *
 * @param settings  Pointer to current settings (populated into form fields).
 *                  Updated and saved when the user submits the form.
 * @return 0 on success, negative on error.
 */
int captive_portal_start(clock_settings_t *settings);

/**
 * @brief Stop the captive portal, AP, and DNS.
 */
void captive_portal_stop(void);

/**
 * @brief Check if settings were submitted via the portal.
 * @return true if the user submitted the config form.
 */
bool captive_portal_settings_received(void);

#endif
