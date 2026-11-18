#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_err.h"
#include <stdbool.h>

/**
 * @brief Initialise WiFi in STA mode.
 *
 * Creates the default STA netif, initialises esp_wifi, and registers
 * internal event handlers. Call once at startup.
 *
 * @return ESP_OK on success.
 */
esp_err_t wifi_manager_init(void);

/**
 * @brief Connect to an access point.
 *
 * Blocks until connected or the timeout expires. On success the device
 * has an IP address and is ready for network operations. On timeout, and
 * after any later drop, the connection keeps retrying in the background
 * with exponential backoff (1 s to 60 s) until wifi_manager_disconnect().
 *
 * @param ssid      SSID (null-terminated, max 32 bytes).
 * @param password  Password (null-terminated, max 64 bytes).
 * @param timeout_ms Maximum wait time in milliseconds; 0 starts connecting
 *                   and returns at once.
 * @return ESP_OK on successful connection, ESP_ERR_TIMEOUT on timeout.
 */
esp_err_t wifi_manager_connect(const char *ssid, const char *password,
                               uint32_t timeout_ms);

/**
 * @brief Disconnect, stop retrying and stop WiFi.
 */
void wifi_manager_disconnect(void);

/**
 * @brief Check if WiFi STA is currently connected with an IP address.
 * @return true if connected.
 */
bool wifi_manager_is_connected(void);

#endif
