#ifndef CLOCK_TIME_H
#define CLOCK_TIME_H

#include "esp_err.h"
#include <stdbool.h>
#include <time.h>

/**
 * @file clock_time.h
 * @brief Display time source: DS1302 RTC disciplined by NTP.
 *
 * The RTC holds UTC and is the clock the display shows. NTP keeps it right:
 * every sync (the first and each hourly refresh) is written to the RTC on
 * a second boundary. When NTP is unreachable the RTC carries on alone, and
 * after a power cut it has the time before WiFi is even up.
 *
 * At boot a valid RTC time also sets the system clock, so anything else
 * that calls time() is right from the start.
 *
 * If no RTC is fitted, or it stops ticking or reads garbage, the display
 * falls back to the system clock (NTP), as it would without an RTC.
 *
 * All functions must be called from the same task.
 */

typedef enum {
    CLOCK_TIME_NONE, /* no valid time yet */
    CLOCK_TIME_RTC,  /* DS1302 */
    CLOCK_TIME_SYSTEM, /* system clock, set by NTP */
} clock_time_source_t;

/**
 * @brief Start the time source.
 * @param use_rtc  false if no DS1302 is fitted (pins are then ignored).
 */
esp_err_t clock_time_init(bool use_rtc, int ce_pin, int sclk_pin, int io_pin);

/**
 * @brief Poll the RTC and copy fresh NTP time into it. Call every 20-50 ms;
 *        the poll rate sets how closely the display follows the RTC tick.
 */
void clock_time_service(void);

/**
 * @brief Current time for display.
 * @param utc   Seconds since the epoch (UTC).
 * @param usec  Microseconds into that second, for blinking.
 * @return false if there is no valid time from any source.
 */
bool clock_time_now(time_t *utc, int *usec);

/** Where clock_time_now() is getting its time from. */
clock_time_source_t clock_time_source(void);

#endif
