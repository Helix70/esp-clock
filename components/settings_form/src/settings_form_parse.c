#include "settings_form.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FORM_MAX 4096

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

static bool form_flag(const char *body, const char *key)
{
    char val[4];
    return get_form_value(body, key, val, sizeof(val));
}

static bool form_int(const char *body, const char *key, int *out)
{
    char val[16];
    if (!get_form_value(body, key, val, sizeof(val))) return false;
    *out = atoi(val);
    return true;
}

char *settings_form_recv(httpd_req_t *req)
{
    if (req->content_len > FORM_MAX) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Form too large");
        return NULL;
    }

    char *body = malloc(req->content_len + 1);
    if (!body) {
        httpd_resp_send_500(req);
        return NULL;
    }

    /* A form can arrive in more than one TCP segment. */
    size_t received = 0;
    while (received < req->content_len) {
        int n = httpd_req_recv(req, body + received, req->content_len - received);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (n <= 0) {
            free(body);
            httpd_resp_send_500(req);
            return NULL;
        }
        received += n;
    }
    body[received] = '\0';
    return body;
}

void settings_form_parse(const char *body, clock_settings_t *s,
                         settings_form_mode_t mode)
{
    char val[16];
    int v;

    if (mode == SETTINGS_FORM_SETUP) {
        get_form_value(body, "ssid", s->wifi_ssid, sizeof(s->wifi_ssid));
        get_form_value(body, "pass", s->wifi_pass, sizeof(s->wifi_pass));
        get_form_value(body, "ota_pass", s->ota_password, sizeof(s->ota_password));
    }
    get_form_value(body, "ntp", s->ntp_server, sizeof(s->ntp_server));
    get_form_value(body, "tz", s->tz, sizeof(s->tz));

    s->time_24h = form_flag(body, "time_24h");
    if (form_int(body, "colon", &v)) s->colon_mode = (uint8_t)v;

    s->auto_brightness = form_flag(body, "auto_br");
    if (form_int(body, "brightness", &v)) s->brightness = (uint8_t)v;
    if (form_int(body, "br_min", &v)) s->bright_min = (uint8_t)v;
    if (form_int(body, "br_max", &v)) s->bright_max = (uint8_t)v;

    if (form_int(body, "color_mode", &v)) s->color_mode = (uint8_t)v;
    for (int i = 0; i < CLOCK_SETTINGS_NUM_COLORS; i++) {
        char key[8];
        snprintf(key, sizeof(key), "color%d", i);
        /* <input type=color> posts "#rrggbb". */
        if (get_form_value(body, key, val, sizeof(val)) && val[0] == '#')
            s->colors[i] = (uint32_t)strtoul(val + 1, NULL, 16);
    }
    if (form_int(body, "fx_speed", &v)) s->effect_speed = (uint16_t)(v < 1 ? 1 : v);

    s->cycle_date = form_flag(body, "cyc_date");
    s->cycle_temp = form_flag(body, "cyc_temp");
    if (form_int(body, "cyc_int", &v)) s->cycle_interval = (uint8_t)(v > 255 ? 255 : (v < 0 ? 0 : v));
    if (form_int(body, "date_mf", &v)) s->date_month_first = v == 1;

    s->fahrenheit = form_flag(body, "fahrenheit");
    /* Degrees with one decimal, stored as tenths. */
    if (get_form_value(body, "temp_offset", val, sizeof(val)) && val[0]) {
        long t = lroundf(strtof(val, NULL) * 10.0f);
        if (t < -CLOCK_TEMP_OFFSET_MAX) t = -CLOCK_TEMP_OFFSET_MAX;
        if (t > CLOCK_TEMP_OFFSET_MAX) t = CLOCK_TEMP_OFFSET_MAX;
        s->temp_offset_tenths = (int16_t)t;
    }

    s->key_beep = form_flag(body, "key_beep");
    s->hourly_chime = form_flag(body, "chime");
}
