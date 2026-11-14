#include "clock_ntp.h"
#include "esp_sntp.h"
#include "esp_log.h"
#include <time.h>

static const char *TAG = "ntp";
static volatile bool s_synced = false;

static void time_sync_cb(struct timeval *tv)
{
    s_synced = true;
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

void clock_ntp_start(const char *server)
{
    ESP_LOGI(TAG, "Starting SNTP, server: %s", server);

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, server);
    sntp_set_time_sync_notification_cb(time_sync_cb);
    esp_sntp_init();
}

void clock_ntp_stop(void)
{
    esp_sntp_stop();
    s_synced = false;
}

bool clock_ntp_is_synced(void)
{
    return s_synced;
}
