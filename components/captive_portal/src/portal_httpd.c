#include "clock_settings.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

#include "portal_internal.h"

static const char *TAG = "httpd";

static httpd_handle_t s_server;
static clock_settings_t *s_settings;
static volatile bool s_submitted;

static void url_decode(char *dst, const char *src, size_t dst_size)
{
    size_t di = 0;
    for (size_t si = 0; src[si] && di < dst_size - 1; si++) {
        if (src[si] == '+') {
            dst[di++] = ' ';
        } else if (src[si] == '%' && src[si + 1] && src[si + 2]) {
            char hex[3] = { src[si + 1], src[si + 2], 0 };
            dst[di++] = (char)strtol(hex, NULL, 16);
            si += 2;
        } else {
            dst[di++] = src[si];
        }
    }
    dst[di] = '\0';
}

static bool get_form_value(const char *body, const char *key,
                           char *val, size_t val_size)
{
    size_t klen = strlen(key);
    const char *p = body;
    while ((p = strstr(p, key)) != NULL) {
        if ((p == body || *(p - 1) == '&') && p[klen] == '=') {
            p += klen + 1;
            const char *end = strchr(p, '&');
            size_t len = end ? (size_t)(end - p) : strlen(p);
            if (len >= val_size) len = val_size - 1;
            char encoded[256];
            if (len >= sizeof(encoded)) len = sizeof(encoded) - 1;
            memcpy(encoded, p, len);
            encoded[len] = '\0';
            url_decode(val, encoded, val_size);
            return true;
        }
        p += klen;
    }
    return false;
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    const char *page = portal_html_get_page(s_settings);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, page, strlen(page));
    portal_html_free_page(page);
    return ESP_OK;
}

/* Append src to dst as a JSON string body, escaping quotes and backslashes. */
static void json_escape(char *dst, size_t dst_size, const char *src)
{
    size_t di = 0;
    for (; *src && di + 2 < dst_size; src++) {
        if (*src == '"' || *src == '\\') dst[di++] = '\\';
        if ((unsigned char)*src < 0x20) continue;
        dst[di++] = *src;
    }
    dst[di] = '\0';
}

/* GET /scan returns cached results; GET /scan?r=1 rescans first. */
static esp_err_t scan_get_handler(httpd_req_t *req)
{
    char q[8];
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK &&
        strstr(q, "r=1"))
        portal_scan();

    size_t n;
    const portal_ap_t *aps = portal_scan_results(&n);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr_chunk(req, "[");
    char item[128], esc[72];
    for (size_t i = 0; i < n; i++) {
        json_escape(esc, sizeof(esc), aps[i].ssid);
        snprintf(item, sizeof(item), "%s{\"ssid\":\"%s\",\"rssi\":%d,\"sec\":%d}",
                 i ? "," : "", esc, aps[i].rssi, aps[i].secure ? 1 : 0);
        httpd_resp_sendstr_chunk(req, item);
    }
    httpd_resp_sendstr_chunk(req, "]");
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

static esp_err_t captive_redirect_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t save_post_handler(httpd_req_t *req)
{
    char *body = malloc(req->content_len + 1);
    if (!body) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    int received = httpd_req_recv(req, body, req->content_len);
    if (received <= 0) {
        free(body);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    body[received] = '\0';

    char val[16];

    get_form_value(body, "ssid", s_settings->wifi_ssid,
                   sizeof(s_settings->wifi_ssid));

    get_form_value(body, "pass", s_settings->wifi_pass,
                   sizeof(s_settings->wifi_pass));

    get_form_value(body, "ntp", s_settings->ntp_server,
                   sizeof(s_settings->ntp_server));

    get_form_value(body, "tz", s_settings->tz, sizeof(s_settings->tz));

    if (get_form_value(body, "brightness", val, sizeof(val)))
        s_settings->brightness = (uint8_t)atoi(val);

    s_settings->fahrenheit = get_form_value(body, "fahrenheit", val, sizeof(val));
    s_settings->time_24h = get_form_value(body, "time_24h", val, sizeof(val));
    s_settings->blink_separator = get_form_value(body, "blink_sep", val, sizeof(val));
    s_settings->led_r = get_form_value(body, "led_r", val, sizeof(val));
    s_settings->led_g = get_form_value(body, "led_g", val, sizeof(val));
    s_settings->led_b = get_form_value(body, "led_b", val, sizeof(val));

    if (get_form_value(body, "temp_offset", val, sizeof(val)))
        s_settings->temp_offset_tenths = (int8_t)atoi(val);

    free(body);

    clock_settings_save(s_settings);
    s_submitted = true;

    ESP_LOGI(TAG, "Settings saved via portal");

    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req,
        "<!DOCTYPE html><html><body style='font-family:sans-serif;"
        "text-align:center;padding:40px'>"
        "<h2>Settings Saved!</h2>"
        "<p>The clock will restart and connect to your WiFi network.</p>"
        "</body></html>");

    return ESP_OK;
}

int portal_httpd_start(clock_settings_t *settings)
{
    s_settings = settings;
    s_submitted = false;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 12;
    config.lru_purge_enable = true;

    if (httpd_start(&s_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return -1;
    }

    httpd_uri_t root = {
        .uri = "/", .method = HTTP_GET, .handler = root_get_handler
    };
    httpd_register_uri_handler(s_server, &root);

    httpd_uri_t save = {
        .uri = "/save", .method = HTTP_POST, .handler = save_post_handler
    };
    httpd_register_uri_handler(s_server, &save);

    httpd_uri_t scan = {
        .uri = "/scan", .method = HTTP_GET, .handler = scan_get_handler
    };
    httpd_register_uri_handler(s_server, &scan);

    const char *redir_uris[] = {
        "/hotspot-detect.html",
        "/generate_204",
        "/connecttest.txt",
        "/redirect",
        "/canonical.html",
        "/ncsi.txt",
    };
    for (int i = 0; i < (int)(sizeof(redir_uris) / sizeof(redir_uris[0])); i++) {
        httpd_uri_t redir = {
            .uri = redir_uris[i], .method = HTTP_GET,
            .handler = captive_redirect_handler
        };
        httpd_register_uri_handler(s_server, &redir);
    }

    ESP_LOGI(TAG, "HTTP server started");
    return 0;
}

void portal_httpd_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}

bool portal_httpd_submitted(void)
{
    return s_submitted;
}
