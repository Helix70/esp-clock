#include "web_server.h"

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "settings_form.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "web";

static httpd_handle_t s_server;
static SemaphoreHandle_t s_lock;
static clock_settings_t s_settings; /* what the page shows, under s_lock */
static web_server_settings_cb_t s_settings_cb;
static bool s_ota_enabled;

static portMUX_TYPE s_status_lock = portMUX_INITIALIZER_UNLOCKED;
static web_server_status_t s_status = {.temp_c = NAN, .source = "none"};

void web_server_publish_status(const web_server_status_t *status)
{
    taskENTER_CRITICAL(&s_status_lock);
    s_status = *status;
    taskEXIT_CRITICAL(&s_status_lock);
}

/* GET /status: {"ok":bool,"ms":UTC ms,"off":offset s,"src":"rtc",
 * "t":Celsius or null,"f":fahrenheit,"h24":24-hour}. The page ticks the
 * seconds itself and re-fetches every few seconds. */
static esp_err_t status_get_handler(httpd_req_t *req)
{
    web_server_status_t st;
    taskENTER_CRITICAL(&s_status_lock);
    st = s_status;
    taskEXIT_CRITICAL(&s_status_lock);

    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool fahrenheit = s_settings.fahrenheit;
    bool h24 = s_settings.time_24h;
    xSemaphoreGive(s_lock);

    int64_t ms = (int64_t)st.utc * 1000 + st.usec / 1000 +
                 (esp_timer_get_time() - st.captured_us) / 1000;
    char temp[16];
    if (isnan(st.temp_c))
        strcpy(temp, "null");
    else
        snprintf(temp, sizeof(temp), "%.2f", (double)st.temp_c);

    char json[160];
    snprintf(json, sizeof(json),
             "{\"ok\":%s,\"ms\":%lld,\"off\":%ld,\"src\":\"%s\",\"t\":%s,"
             "\"f\":%s,\"h24\":%s}",
             st.time_valid ? "true" : "false", (long long)ms,
             (long)st.utc_offset_s, st.source, temp,
             fahrenheit ? "true" : "false", h24 ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    char *page = settings_form_page(&s_settings, SETTINGS_FORM_NETWORK, s_ota_enabled);
    xSemaphoreGive(s_lock);
    if (!page) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send(req, page, strlen(page));
    free(page);
    return ESP_OK;
}

static esp_err_t save_post_handler(httpd_req_t *req)
{
    char *body = settings_form_recv(req);
    if (!body) return ESP_FAIL;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    clock_settings_t s = s_settings;
    settings_form_parse(body, &s, SETTINGS_FORM_NETWORK);
    free(body);
    clock_settings_sanitize(&s);
    int err = clock_settings_save(&s);
    s_settings = s;
    xSemaphoreGive(s_lock);

    if (s_settings_cb) s_settings_cb(&s);

    httpd_resp_set_type(req, "text/plain");
    if (err != 0) {
        ESP_LOGE(TAG, "Settings applied but not saved: %s", esp_err_to_name(err));
        httpd_resp_sendstr(req, "Applied, but saving failed: they will be lost on restart");
        return ESP_OK;
    }
    ESP_LOGI(TAG, "Settings saved from the web page");
    httpd_resp_sendstr(req, "Saved");
    return ESP_OK;
}

esp_err_t web_server_start(const clock_settings_t *settings,
                           web_server_settings_cb_t settings_cb,
                           ota_server_status_cb_t ota_cb)
{
    if (s_server) return ESP_OK;
    if (!s_lock && !(s_lock = xSemaphoreCreateMutex())) return ESP_ERR_NO_MEM;

    s_settings = *settings;
    s_settings_cb = settings_cb;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 6144;     /* firmware upload */
    config.recv_wait_timeout = 10;
    config.lru_purge_enable = true;
    if (httpd_start(&s_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return ESP_FAIL;
    }

    httpd_uri_t root = {.uri = "/", .method = HTTP_GET, .handler = root_get_handler};
    httpd_register_uri_handler(s_server, &root);
    httpd_uri_t save = {.uri = "/save", .method = HTTP_POST, .handler = save_post_handler};
    httpd_register_uri_handler(s_server, &save);
    httpd_uri_t status = {.uri = "/status", .method = HTTP_GET, .handler = status_get_handler};
    httpd_register_uri_handler(s_server, &status);

    s_ota_enabled = settings->ota_password[0] &&
                    ota_server_register(s_server, settings->ota_password, ota_cb) == ESP_OK;
    if (!s_ota_enabled)
        ESP_LOGI(TAG, "Firmware upload off (no OTA password set)");

    ESP_LOGI(TAG, "Settings page started");
    return ESP_OK;
}

void web_server_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}
