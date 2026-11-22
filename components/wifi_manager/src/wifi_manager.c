#include "wifi_manager.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include <string.h>

static const char *TAG = "wifi_mgr";

#define CONNECTED_BIT BIT0

/* Reconnect backoff: 1 s doubling to 60 s, retried forever. A clock has
 * nothing better to do than wait for the router to come back. */
#define BACKOFF_MIN_MS 1000
#define BACKOFF_MAX_MS 60000

static EventGroupHandle_t s_event_group;
static esp_timer_handle_t s_retry_timer;
static volatile bool s_connected;
static volatile bool s_active; /* false after disconnect(): stop retrying */
static uint32_t s_backoff_ms = BACKOFF_MIN_MS;

static void retry_cb(void *arg)
{
    (void)arg;
    if (s_active) {
        ESP_LOGI(TAG, "Reconnecting...");
        esp_wifi_connect();
    }
}

static void on_wifi_event(void *arg, esp_event_base_t base,
                          int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_STA_START) {
            /* Not after disconnect(): the portal restarts WiFi in APSTA
             * mode and must not have the STA chasing the old network. */
            if (s_active) esp_wifi_connect();
        } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
            bool was_connected = s_connected;
            s_connected = false;
            xEventGroupClearBits(s_event_group, CONNECTED_BIT);
            if (!s_active) return;

            if (was_connected) s_backoff_ms = BACKOFF_MIN_MS;
            wifi_event_sta_disconnected_t *ev = data;
            ESP_LOGW(TAG, "Disconnected (reason %d), retry in %lu ms",
                     ev->reason, (unsigned long)s_backoff_ms);
            esp_timer_stop(s_retry_timer);
            esp_timer_start_once(s_retry_timer, (uint64_t)s_backoff_ms * 1000);
            s_backoff_ms *= 2;
            if (s_backoff_ms > BACKOFF_MAX_MS) s_backoff_ms = BACKOFF_MAX_MS;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&ev->ip_info.ip));
        s_connected = true;
        s_backoff_ms = BACKOFF_MIN_MS;
        xEventGroupSetBits(s_event_group, CONNECTED_BIT);
    }
}

esp_err_t wifi_manager_init(void)
{
    s_event_group = xEventGroupCreate();

    const esp_timer_create_args_t targs = {
        .callback = retry_cb,
        .name = "wifi_retry",
    };
    ESP_ERROR_CHECK(esp_timer_create(&targs, &s_retry_timer));

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &on_wifi_event, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    return ESP_OK;
}

esp_err_t wifi_manager_connect(const char *ssid, const char *pass,
                               uint32_t timeout_ms)
{
    s_connected = false;
    s_active = true;
    s_backoff_ms = BACKOFF_MIN_MS;
    xEventGroupClearBits(s_event_group, CONNECTED_BIT);

    wifi_config_t sta_cfg = {0};
    strncpy((char *)sta_cfg.sta.ssid, ssid,
            sizeof(sta_cfg.sta.ssid) - 1);
    strncpy((char *)sta_cfg.sta.password, pass,
            sizeof(sta_cfg.sta.password) - 1);
    /* An empty password means an open network. */
    sta_cfg.sta.threshold.authmode =
        pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    /* With several APs on the same SSID (mesh, extenders, 2.4 + 5 GHz),
     * scan every channel and join the strongest one instead of the first
     * one found. This applies to every connect, including the retries
     * after a disconnect. */
    sta_cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    sta_cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to '%s'...", ssid);

    EventBits_t bits = xEventGroupWaitBits(s_event_group, CONNECTED_BIT,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(timeout_ms));
    if (bits & CONNECTED_BIT) {
        return ESP_OK;
    }

    if (timeout_ms > 0)
        ESP_LOGW(TAG, "Not connected yet, still retrying in the background");
    return ESP_ERR_TIMEOUT;
}

void wifi_manager_disconnect(void)
{
    s_active = false;
    esp_timer_stop(s_retry_timer);
    esp_wifi_disconnect();
    esp_wifi_stop();
    s_connected = false;
}

bool wifi_manager_is_connected(void)
{
    return s_connected;
}
