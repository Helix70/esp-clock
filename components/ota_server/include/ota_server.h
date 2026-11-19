#ifndef OTA_SERVER_H
#define OTA_SERVER_H

#include "esp_err.h"
#include "esp_http_server.h"
#include <stdbool.h>

/**
 * @file ota_server.h
 * @brief Firmware update over WiFi.
 *
 * Adds POST /update to an HTTP server. An upload must carry the configured
 * password in the X-OTA-Password header. The new image goes to the
 * inactive OTA slot and the clock restarts into it once it has been
 * verified. The web_server page uploads from a browser; from a terminal:
 *
 *     curl -H "X-OTA-Password: <password>" --data-binary @build/esp-clock.bin  *          http://<clock-ip>/update
 *
 * The server needs a stack of at least 6 KB and a receive timeout long
 * enough for a slow upload.
 */

/** Called with true when an upload starts, false if it fails. */
typedef void (*ota_server_status_cb_t)(bool active);

/**
 * @brief Register the upload handler.
 * @param server    Running HTTP server.
 * @param password  Required upload password; must be non-empty.
 * @param status_cb Optional upload status callback.
 */
esp_err_t ota_server_register(httpd_handle_t server, const char *password,
                              ota_server_status_cb_t status_cb);

#endif
