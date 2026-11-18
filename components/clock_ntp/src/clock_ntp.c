#include "clock_ntp.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include <string.h>
#include <time.h>

static const char *TAG = "ntp";

static volatile bool s_synced = false;
static volatile uint32_t s_sync_count = 0;
static bool s_started = false;
static char s_server[65];

/* Anything before this is the RTC counting from 1970 after power-on. */
#define VALID_EPOCH 1735689600 /* 2025-01-01 */

static void time_sync_cb(struct timeval *tv)
{
    s_synced = true;
    s_sync_count++;

    time_t now = time(NULL);
    struct tm t;
    gmtime_r(&now, &t);
    ESP_LOGI(TAG, "Time synced: %04d-%02d-%02d %02d:%02d:%02d UTC",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
             t.tm_hour, t.tm_min, t.tm_sec);

    /* Local time per the TZ environment variable set from settings. */
    char local[48];
    localtime_r(&now, &t);
    strftime(local, sizeof(local), "%Y-%m-%d %H:%M:%S %Z (UTC%z)", &t);
    ESP_LOGI(TAG, "Local time:  %s", local);
}

/* A fresh IP means the network just came (back) up: ask for the time now
 * rather than waiting out SNTP's own retry backoff. */
static void on_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (s_started && esp_sntp_enabled()) {
        ESP_LOGI(TAG, "Network up, requesting time");
        esp_sntp_restart();
    }
}

void clock_ntp_start(const char *server)
{
    ESP_LOGI(TAG, "Starting SNTP, server: %s", server);

    /* SNTP keeps the pointer, not a copy. */
    strlcpy(s_server, server, sizeof(s_server));
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, s_server);
    sntp_set_time_sync_notification_cb(time_sync_cb);
    esp_sntp_init();

    if (!s_started) {
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                            &on_got_ip, NULL, NULL);
    }
    s_started = true;
}

void clock_ntp_set_server(const char *server)
{
    ESP_LOGI(TAG, "NTP server changed to %s", server);
    esp_sntp_stop();
    strlcpy(s_server, server, sizeof(s_server));
    esp_sntp_setservername(0, s_server);
    esp_sntp_init();
}

void clock_ntp_stop(void)
{
    esp_sntp_stop();
    s_synced = false;
}

uint32_t clock_ntp_sync_count(void)
{
    return s_sync_count;
}

bool clock_ntp_is_synced(void)
{
    /* System time survives a software reset, so a restart after the first
     * sync still has a valid clock before NTP answers again. */
    return s_synced || time(NULL) >= VALID_EPOCH;
}
