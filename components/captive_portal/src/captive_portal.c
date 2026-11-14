#include "captive_portal.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include <stdlib.h>
#include <string.h>

#include "portal_internal.h"

static const char *TAG = "portal";

static TaskHandle_t s_dns_task;
static bool s_running;

static portal_ap_t s_aps[PORTAL_SCAN_MAX];
static size_t s_ap_count;

int portal_scan(void)
{
    wifi_scan_config_t scan_cfg = {
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    };
    esp_err_t err = esp_wifi_scan_start(&scan_cfg, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Scan failed: %s", esp_err_to_name(err));
        return -1;
    }

    uint16_t n = 0;
    esp_wifi_scan_get_ap_num(&n);
    if (n == 0) {
        s_ap_count = 0;
        return 0;
    }

    wifi_ap_record_t *recs = calloc(n, sizeof(*recs));
    if (!recs) {
        esp_wifi_clear_ap_list();
        return -1;
    }
    esp_wifi_scan_get_ap_records(&n, recs);

    /* Records arrive strongest first; keep the first of each SSID. */
    s_ap_count = 0;
    for (uint16_t i = 0; i < n && s_ap_count < PORTAL_SCAN_MAX; i++) {
        const char *ssid = (const char *)recs[i].ssid;
        if (ssid[0] == '\0') continue;
        bool dup = false;
        for (size_t j = 0; j < s_ap_count; j++) {
            if (strcmp(s_aps[j].ssid, ssid) == 0) { dup = true; break; }
        }
        if (dup) continue;
        portal_ap_t *ap = &s_aps[s_ap_count++];
        strlcpy(ap->ssid, ssid, sizeof(ap->ssid));
        ap->rssi = recs[i].rssi;
        ap->secure = recs[i].authmode != WIFI_AUTH_OPEN;
    }
    free(recs);

    ESP_LOGI(TAG, "Scan found %u APs (%u unique)", n, (unsigned)s_ap_count);
    return (int)s_ap_count;
}

const portal_ap_t *portal_scan_results(size_t *count)
{
    *count = s_ap_count;
    return s_aps;
}

static void dns_redirect_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "DNS socket failed");
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in bind_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(53),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr));

    struct timeval timeout = { .tv_sec = 1, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    uint8_t pkt[512];
    while (s_running) {
        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);
        int n = recvfrom(sock, pkt, sizeof(pkt), 0,
                         (struct sockaddr *)&from, &from_len);
        if (n < 12) continue;

        /* Set response flags and answer count */
        pkt[2] = 0x81;
        pkt[3] = 0x80;
        pkt[6] = 0x00; pkt[7] = 0x01;

        /* Find end of question section */
        int off = 12;
        while (off < n && pkt[off] != 0) off += pkt[off] + 1;
        off += 5;

        /* Append A record answer pointing to our AP IP */
        if (off + 16 <= (int)sizeof(pkt)) {
            pkt[off++] = 0xC0; pkt[off++] = 0x0C;
            pkt[off++] = 0x00; pkt[off++] = 0x01;
            pkt[off++] = 0x00; pkt[off++] = 0x01;
            pkt[off++] = 0x00; pkt[off++] = 0x00;
            pkt[off++] = 0x00; pkt[off++] = 0x3C;
            pkt[off++] = 0x00; pkt[off++] = 0x04;
            pkt[off++] = 192;  pkt[off++] = 168;
            pkt[off++] = 4;    pkt[off++] = 1;

            sendto(sock, pkt, off, 0,
                   (struct sockaddr *)&from, from_len);
        }
    }

    close(sock);
    vTaskDelete(NULL);
}

int captive_portal_start(clock_settings_t *settings)
{
    ESP_LOGI(TAG, "Starting captive portal");

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    char ap_ssid[32];
    snprintf(ap_ssid, sizeof(ap_ssid), "ESP-Clock-%02X%02X",
             mac[4], mac[5]);

    esp_netif_create_default_wifi_ap();
    /* A STA interface is needed to scan; reuse it if wifi_manager made one. */
    if (!esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"))
        esp_netif_create_default_wifi_sta();

    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&wifi_cfg);
    if (err != ESP_OK && err != ESP_ERR_WIFI_INIT_STATE) {
        ESP_ERROR_CHECK(err);
    }

    wifi_config_t ap_config = {
        .ap = {
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN,
            .channel = 1,
        },
    };
    strncpy((char *)ap_config.ap.ssid, ap_ssid, sizeof(ap_config.ap.ssid));
    ap_config.ap.ssid_len = strlen(ap_ssid);

    /* APSTA so the STA side can scan while the AP serves the portal. */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "AP started: %s", ap_ssid);

    /* Scan once up front, before any client connects, so the first page
     * load has a network list and the AP is not disturbed mid-session. */
    portal_scan();

    s_running = true;
    xTaskCreate(dns_redirect_task, "dns_redir", 4096, NULL, 3, &s_dns_task);

    portal_httpd_start(settings);

    return 0;
}

void captive_portal_stop(void)
{
    s_running = false;
    portal_httpd_stop();
    esp_wifi_stop();
    ESP_LOGI(TAG, "Captive portal stopped");
}

bool captive_portal_settings_received(void)
{
    return portal_httpd_submitted();
}
