#ifndef PORTAL_INTERNAL_H
#define PORTAL_INTERNAL_H

#include "clock_settings.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* --- Timezone table (portal_tz.c) --- */
typedef struct {
    const char *label;
    const char *posix;
} portal_tz_t;

const portal_tz_t *portal_tz_table(size_t *count);

/* --- WiFi scan cache (captive_portal.c) --- */
#define PORTAL_SCAN_MAX 20

typedef struct {
    char ssid[33];
    int8_t rssi;
    bool secure;
} portal_ap_t;

/* Blocking scan; refreshes the cache. Returns number of APs found. */
int portal_scan(void);

/* Read the cached scan results (deduplicated, strongest first). */
const portal_ap_t *portal_scan_results(size_t *count);

/* --- HTML (portal_html.c) --- */
const char *portal_html_get_page(const clock_settings_t *s);
void portal_html_free_page(const char *page);

/* --- HTTP server (portal_httpd.c) --- */
int portal_httpd_start(clock_settings_t *settings);
void portal_httpd_stop(void);
bool portal_httpd_submitted(void);

#endif
