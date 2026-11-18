#ifndef DS1302_H
#define DS1302_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @file ds1302.h
 * @brief DS1302 real-time clock over its 3-wire interface (CE, SCLK, I/O).
 *
 * Bit-banged on plain GPIOs. Data is LSB first; the DS1302 samples I/O on
 * SCLK rising edges and drives it after falling edges during reads.
 *
 * Not thread safe: call from one task.
 */

typedef struct {
    int ce;
    int sclk;
    int io;
} ds1302_t;

typedef struct {
    uint8_t seconds; /* 0-59 */
    uint8_t minutes; /* 0-59 */
    uint8_t hours;   /* 0-23 */
    uint8_t day;     /* 1-31 */
    uint8_t month;   /* 1-12 */
    uint8_t weekday; /* 1-7, 1 = Sunday */
    uint8_t year;    /* 0-99, offset from 2000 */
} ds1302_time_t;

/** Battery-backed RAM size in bytes. */
#define DS1302_RAM_SIZE 31

/**
 * @brief Configure the pins and make sure the trickle charger is off.
 *
 * Does not start a halted oscillator: a halted clock means the time was
 * lost, and ds1302_get_time() reports that until ds1302_set_time().
 */
esp_err_t ds1302_init(ds1302_t *dev, int ce_pin, int sclk_pin, int io_pin);

/**
 * @brief Read all clock registers in one burst (latched together).
 * @return false if the oscillator is halted or any field is out of range.
 */
bool ds1302_get_time(ds1302_t *dev, ds1302_time_t *time);

/**
 * @brief Read just the seconds register (cheap, for spotting the tick).
 * @return 0-59, or -1 if the oscillator is halted or the value is invalid.
 */
int ds1302_get_seconds(ds1302_t *dev);

/**
 * @brief Write all clock registers in one burst, in 24-hour mode, and start
 *        the oscillator. Writing the seconds resets the sub-second chain, so
 *        call this right on a second boundary.
 */
void ds1302_set_time(ds1302_t *dev, const ds1302_time_t *time);

/** Read one byte of battery-backed RAM, @p addr 0-30. */
uint8_t ds1302_read_ram(ds1302_t *dev, uint8_t addr);

/** Write one byte of battery-backed RAM, @p addr 0-30. */
void ds1302_write_ram(ds1302_t *dev, uint8_t addr, uint8_t data);

#endif
