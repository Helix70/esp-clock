#include "clock_time.h"

#include "clock_ntp.h"
#include "ds1302.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <sys/time.h>

static const char *TAG = "clock_time";

/*
 * Battery-backed RAM byte written alongside the time when NTP sets the RTC.
 * A halted oscillator already flags a flat cell, but the marker also rejects
 * time left behind by other firmware (the stock STC8G kept local time).
 */
#define RAM_MARKER_ADDR 0
#define RAM_MARKER 0xC5

/* The RTC is treated as stopped if its seconds have not moved for this
 * long, e.g. a dead crystal or a disconnected I/O line. */
#define RTC_STALL_US (2500 * 1000)

/* How often a dropped RTC is re-checked. */
#define RTC_RETRY_US (5 * 1000 * 1000)

static ds1302_t s_rtc;
static bool s_use_rtc;
static bool s_rtc_ok;          /* RTC is set and ticking */
static time_t s_rtc_utc;       /* RTC time at the last tick */
static int64_t s_rtc_tick_us;  /* esp_timer time of that tick */
static int s_rtc_last_sec = -1;
static int64_t s_retry_us;

static uint32_t s_seen_syncs;
static bool s_write_pending;
static time_t s_last_sys_sec;

/* Days since 1970-01-01 for a civil date (proleptic Gregorian). */
static int64_t days_from_civil(int y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

static time_t rtc_to_utc(const ds1302_time_t *t)
{
    int64_t days = days_from_civil(2000 + t->year, t->month, t->day);
    return (time_t)(days * 86400 + t->hours * 3600 + t->minutes * 60 + t->seconds);
}

static void utc_to_rtc(time_t utc, ds1302_time_t *t)
{
    struct tm tm;
    gmtime_r(&utc, &tm);
    t->seconds = (uint8_t)tm.tm_sec;
    t->minutes = (uint8_t)tm.tm_min;
    t->hours = (uint8_t)tm.tm_hour;
    t->day = (uint8_t)tm.tm_mday;
    t->month = (uint8_t)(tm.tm_mon + 1);
    t->weekday = (uint8_t)(tm.tm_wday + 1); /* DS1302: 1 = Sunday */
    t->year = (uint8_t)(tm.tm_year - 100);
}

/* Full read; true and the UTC time if the RTC holds a time we trust. */
static bool rtc_read(time_t *utc)
{
    ds1302_time_t t;
    if (!ds1302_get_time(&s_rtc, &t)) return false;
    if (ds1302_read_ram(&s_rtc, RAM_MARKER_ADDR) != RAM_MARKER) return false;
    *utc = rtc_to_utc(&t);
    return true;
}

esp_err_t clock_time_init(bool use_rtc, int ce_pin, int sclk_pin, int io_pin)
{
    s_use_rtc = use_rtc;
    s_rtc_ok = false;
    s_rtc_utc = 0;
    s_rtc_last_sec = -1;
    s_write_pending = false;
    s_last_sys_sec = 0;
    s_seen_syncs = clock_ntp_sync_count();
    if (!use_rtc) {
        ESP_LOGI(TAG, "No RTC fitted, display follows NTP");
        return ESP_OK;
    }

    esp_err_t err = ds1302_init(&s_rtc, ce_pin, sclk_pin, io_pin);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "DS1302 init failed: %s", esp_err_to_name(err));
        s_use_rtc = false;
        return err;
    }

    time_t utc;
    if (!rtc_read(&utc)) {
        ESP_LOGW(TAG, "RTC has no valid time (first use, flat cell or not "
                      "connected); waiting for NTP");
        return ESP_OK;
    }

    /* Seed the system clock so time() is right before NTP answers. */
    struct timeval tv = {.tv_sec = utc, .tv_usec = 0};
    settimeofday(&tv, NULL);

    s_rtc_ok = true;
    s_rtc_utc = utc;
    s_rtc_tick_us = esp_timer_get_time();
    s_rtc_last_sec = (int)(utc % 60);

    struct tm tm;
    gmtime_r(&utc, &tm);
    ESP_LOGI(TAG, "RTC time %04d-%02d-%02d %02d:%02d:%02d UTC loaded",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec);
    return ESP_OK;
}

/* Write the system time into the RTC as soon as a new second starts, so
 * the RTC's sub-second chain (reset by the write) lines up with NTP. */
static void write_rtc_on_second_boundary(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);

    if (s_last_sys_sec == 0) {
        s_last_sys_sec = tv.tv_sec;
        return;
    }
    if (tv.tv_sec == s_last_sys_sec) return;
    s_last_sys_sec = 0;

    ds1302_time_t t;
    utc_to_rtc(tv.tv_sec, &t);
    ds1302_set_time(&s_rtc, &t);
    ds1302_write_ram(&s_rtc, RAM_MARKER_ADDR, RAM_MARKER);

    time_t check;
    if (!rtc_read(&check) || check < tv.tv_sec || check > tv.tv_sec + 1) {
        ESP_LOGE(TAG, "RTC write did not read back; check the DS1302 wiring");
        s_rtc_ok = false;
    } else {
        if (!s_rtc_ok)
            ESP_LOGI(TAG, "RTC set from NTP");
        s_rtc_ok = true;
        s_rtc_utc = tv.tv_sec;
        s_rtc_tick_us = esp_timer_get_time() - tv.tv_usec;
        s_rtc_last_sec = (int)(tv.tv_sec % 60);
    }
    s_write_pending = false;
}

void clock_time_service(void)
{
    if (!s_use_rtc) return;

    uint32_t syncs = clock_ntp_sync_count();
    if (syncs != s_seen_syncs) {
        s_seen_syncs = syncs;
        s_write_pending = true;
    }
    if (s_write_pending) {
        write_rtc_on_second_boundary();
        return;
    }

    int64_t now_us = esp_timer_get_time();

    if (!s_rtc_ok) {
        /* Dropped while offline: try again now and then rather than wait
         * for the next NTP sync. It counts as back once it ticks. */
        if (now_us - s_retry_us < RTC_RETRY_US) return;
        s_retry_us = now_us;
        time_t utc;
        if (rtc_read(&utc) && utc != s_rtc_utc) {
            if (s_rtc_utc != 0) {
                ESP_LOGI(TAG, "RTC ticking again");
                s_rtc_ok = true;
                s_rtc_tick_us = now_us;
                s_rtc_last_sec = (int)(utc % 60);
            }
            s_rtc_utc = utc;
        }
        return;
    }

    int sec = ds1302_get_seconds(&s_rtc);
    if (sec >= 0 && sec != s_rtc_last_sec) {
        /* A new RTC second: re-read everything and timestamp the tick.
         * A one-off bad read is ignored; only a stall drops the RTC. */
        time_t utc;
        if (rtc_read(&utc)) {
            s_rtc_utc = utc;
            s_rtc_tick_us = now_us;
            s_rtc_last_sec = sec;
        }
        return;
    }

    if (now_us - s_rtc_tick_us > RTC_STALL_US) {
        ESP_LOGW(TAG, "RTC stopped or unreadable, falling back to system time");
        s_rtc_ok = false;
        s_retry_us = now_us;
    }
}

bool clock_time_now(time_t *utc, int *usec)
{
    if (s_rtc_ok) {
        int64_t since = esp_timer_get_time() - s_rtc_tick_us;
        if (since < 0) since = 0;
        *utc = s_rtc_utc + (time_t)(since / 1000000);
        *usec = (int)(since % 1000000);
        return true;
    }

    if (clock_ntp_is_synced()) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        *utc = tv.tv_sec;
        *usec = (int)tv.tv_usec;
        return true;
    }

    return false;
}

clock_time_source_t clock_time_source(void)
{
    if (s_rtc_ok) return CLOCK_TIME_RTC;
    return clock_ntp_is_synced() ? CLOCK_TIME_SYSTEM : CLOCK_TIME_NONE;
}
