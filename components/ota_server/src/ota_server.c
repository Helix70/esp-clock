#include "ota_server.h"

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"

#include <stdlib.h>
#include <string.h>

static const char *TAG = "ota";

#define CHUNK_SIZE 4096

static char s_password[64];
static ota_server_status_cb_t s_status_cb;
static volatile bool s_busy;

/* Compare without an early exit, so timing does not leak the password. */
static bool password_ok(const char *given)
{
    size_t a = strlen(given), b = strlen(s_password);
    unsigned diff = (unsigned)(a ^ b);
    for (size_t i = 0; i < b; i++)
        diff |= (unsigned)(s_password[i] ^ (i < a ? given[i] : 0));
    return diff == 0;
}

static void restart_cb(void *arg)
{
    (void)arg;
    esp_restart();
}

static esp_err_t fail(httpd_req_t *req, const char *status, const char *msg)
{
    ESP_LOGE(TAG, "%s", msg);
    s_busy = false;
    if (s_status_cb) s_status_cb(false);
    httpd_resp_set_status(req, status);
    httpd_resp_sendstr(req, msg);
    return ESP_FAIL;
}

static esp_err_t update_post_handler(httpd_req_t *req)
{
    char given[sizeof(s_password)] = {0};
    if (httpd_req_get_hdr_value_str(req, "X-OTA-Password", given,
                                    sizeof(given)) != ESP_OK ||
        !password_ok(given)) {
        ESP_LOGW(TAG, "Upload rejected: bad password");
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_sendstr(req, "Wrong OTA password");
        return ESP_OK;
    }

    if (s_busy) {
        httpd_resp_set_status(req, "409 Conflict");
        httpd_resp_sendstr(req, "An update is already in progress");
        return ESP_OK;
    }
    s_busy = true;

    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    if (!target) {
        s_busy = false;
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "No OTA partition (flash the OTA partition table over USB first)");
        return ESP_FAIL;
    }
    if (req->content_len > target->size) {
        s_busy = false;
        httpd_resp_set_status(req, "413 Payload Too Large");
        httpd_resp_sendstr(req, "Image is larger than the OTA slot");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Receiving %u bytes into %s", (unsigned)req->content_len,
             target->label);
    if (s_status_cb) s_status_cb(true);

    char *buf = malloc(CHUNK_SIZE);
    if (!buf) return fail(req, "500 Internal Server Error", "Out of memory");

    esp_ota_handle_t ota;
    esp_err_t err = esp_ota_begin(target, OTA_SIZE_UNKNOWN, &ota);
    if (err != ESP_OK) {
        free(buf);
        return fail(req, "500 Internal Server Error", "esp_ota_begin failed");
    }

    size_t remaining = req->content_len;
    while (remaining > 0) {
        int n = httpd_req_recv(req, buf, remaining < CHUNK_SIZE ? remaining : CHUNK_SIZE);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (n <= 0) {
            esp_ota_abort(ota);
            free(buf);
            return fail(req, "400 Bad Request", "Upload interrupted");
        }
        if (esp_ota_write(ota, buf, n) != ESP_OK) {
            esp_ota_abort(ota);
            free(buf);
            return fail(req, "400 Bad Request", "Not a valid firmware image");
        }
        remaining -= n;
    }
    free(buf);

    if (esp_ota_end(ota) != ESP_OK)
        return fail(req, "400 Bad Request", "Image failed verification");
    if (esp_ota_set_boot_partition(target) != ESP_OK)
        return fail(req, "500 Internal Server Error", "Could not select new image");

    ESP_LOGI(TAG, "Update complete, restarting");
    httpd_resp_sendstr(req, "Update complete, restarting...");

    /* Restart after the response has gone out. */
    static esp_timer_handle_t timer;
    const esp_timer_create_args_t targs = {.callback = restart_cb, .name = "ota_rst"};
    if (esp_timer_create(&targs, &timer) == ESP_OK)
        esp_timer_start_once(timer, 1000 * 1000);
    else
        esp_restart();
    return ESP_OK;
}

esp_err_t ota_server_register(httpd_handle_t server, const char *password,
                             ota_server_status_cb_t status_cb)
{
    if (!password || !password[0]) return ESP_ERR_INVALID_ARG;

    strlcpy(s_password, password, sizeof(s_password));
    s_status_cb = status_cb;

    httpd_uri_t update = {.uri = "/update", .method = HTTP_POST, .handler = update_post_handler};
    esp_err_t err = httpd_register_uri_handler(server, &update);
    if (err == ESP_OK) ESP_LOGI(TAG, "Firmware upload enabled");
    return err;
}
