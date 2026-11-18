#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "clock_settings.h"
#include "esp_err.h"
#include "ota_server.h"

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

#endif
