#ifndef CLOCK_NTP_H
#define CLOCK_NTP_H

#include <stdbool.h>

/**
 * @brief Start SNTP time synchronisation.
 *
 * Configures esp_sntp in poll mode. Sync interval is controlled by
 * CONFIG_LWIP_SNTP_UPDATE_DELAY (default 1 hour in sdkconfig.defaults).
 * After the first sync, system time() returns UTC.
 *
 * @param server  NTP server hostname (e.g. "pool.ntp.org").
 */
void clock_ntp_start(const char *server);

/**
 * @brief Stop SNTP.
 */
void clock_ntp_stop(void);

/**
 * @brief Check if time has been synchronised at least once.
 * @return true if system time is valid.
 */
bool clock_ntp_is_synced(void);

#endif
