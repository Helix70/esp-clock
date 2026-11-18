#ifndef CLOCK_NTP_H
#define CLOCK_NTP_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Start SNTP time synchronisation.
 *
 * Configures esp_sntp in poll mode. Sync interval is controlled by
 * CONFIG_LWIP_SNTP_UPDATE_DELAY (default 1 hour in sdkconfig.defaults).
 * After the first sync, system time() returns UTC. Whenever the station
 * gets an IP address (including after a reconnect) a new request is sent
 * straight away.
 *
 * @param server  NTP server hostname (e.g. "pool.ntp.org").
 */
void clock_ntp_start(const char *server);

/**
 * @brief Switch to another NTP server and sync from it straight away.
 */
void clock_ntp_set_server(const char *server);

/**
 * @brief Stop SNTP.
 */
void clock_ntp_stop(void);

/**
 * @brief Check whether the system time is valid.
 * @return true once NTP has synced, or if the clock is already set from
 *         before a software reset.
 */
bool clock_ntp_is_synced(void);

/**
 * @brief Number of successful NTP syncs since boot.
 *
 * Increments on every sync, including the periodic refreshes, so a caller
 * can spot a fresh time by comparing against the last value it saw.
 */
uint32_t clock_ntp_sync_count(void);

#endif
