#ifndef HU058_H
#define HU058_H

#include "aip33628.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @file hu058.h
 * @brief HU-058 LED clock display abstraction.
 *
 * The HU-058 PCB mounts two AiP33628 drivers controlling four
 * seven-segment digits (hours tens/ones, minutes tens/ones) plus
 * five annunciators (AM, colon upper/lower, dash, degree).
 *
 * Each segment position is an RGB LED (3 SEG lines per LED).
 * The display is multiplexed at 1/4 duty using shorted COM pairs.
 *
 * Typical usage:
 *
 *     aip33628_t drv_h, drv_m;
 *     aip33628_init(&drv_h, PIN_H_DIN, PIN_H_CLK);
 *     aip33628_init(&drv_m, PIN_M_DIN, PIN_M_CLK);
 *
 *     hu058_display_t disp;
 *     hu058_init(&disp, &drv_h, &drv_m);
 *     hu058_set_time(&disp, 12, 34);
 *     hu058_set_brightness(&disp, 8);
 *     hu058_compile(&disp);
 *
 *     // In a timer ISR at ~1.6 kHz for flicker-free display:
 *     hu058_scan_step(&disp);
 */

/** Digit positions. */
enum hu058_pos {
    HU058_H_TENS = 0,
    HU058_H_ONES = 1,
    HU058_M_TENS = 2,
    HU058_M_ONES = 3,
    HU058_NUM_DIGITS = 4
};

/** 12-hour or 24-hour time format. */
enum hu058_time_format {
    HU058_24H = 0,
    HU058_12H = 1,
};

/** Annunciator identifiers. */
enum hu058_ann {
    HU058_ANN_AM = 0,
    HU058_ANN_COLON_UPPER = 1,
    HU058_ANN_COLON_LOWER = 2,
    HU058_ANN_DASH = 3,
    HU058_ANN_DEGREE = 4,
    HU058_NUM_ANN = 5
};

/** Seven-segment bitmask bits (bit 0 = segment a). */
enum hu058_seg {
    HU058_SEG_A = (1 << 0),
    HU058_SEG_B = (1 << 1),
    HU058_SEG_C = (1 << 2),
    HU058_SEG_D = (1 << 3),
    HU058_SEG_E = (1 << 4),
    HU058_SEG_F = (1 << 5),
    HU058_SEG_G = (1 << 6),
};

/** Per-channel on/off colour. */
typedef struct {
    bool r;
    bool g;
    bool b;
} hu058_color_t;

#define HU058_COLOR_RED   ((hu058_color_t){true, false, false})
#define HU058_COLOR_GREEN ((hu058_color_t){false, true, false})
#define HU058_COLOR_BLUE  ((hu058_color_t){false, false, true})
#define HU058_COLOR_WHITE ((hu058_color_t){true, true, true})
#define HU058_COLOR_OFF   ((hu058_color_t){false, false, false})

/** Perceptually linear brightness levels (gamma ~2.2). */
#define HU058_BRIGHTNESS_1  0
#define HU058_BRIGHTNESS_2  1
#define HU058_BRIGHTNESS_3  3
#define HU058_BRIGHTNESS_4  8
#define HU058_BRIGHTNESS_5  15

/** Display state. */
typedef struct {
    aip33628_t *drv[2];

    uint8_t digits[HU058_NUM_DIGITS];
    hu058_color_t digit_color[HU058_NUM_DIGITS];

    bool ann[HU058_NUM_ANN];
    hu058_color_t ann_color[HU058_NUM_ANN];

    enum hu058_time_format time_format;

    uint8_t current;
    uint8_t scan_phase;

    uint16_t seg_buf[2][4];
    uint8_t com_buf[4];
} hu058_display_t;

void hu058_init(hu058_display_t *disp, aip33628_t *drv_h, aip33628_t *drv_m);
void hu058_set_digit(hu058_display_t *disp, enum hu058_pos pos, uint8_t value);
void hu058_set_segments(hu058_display_t *disp, enum hu058_pos pos, uint8_t segs);
void hu058_set_digit_color(hu058_display_t *disp, enum hu058_pos pos, hu058_color_t color);
void hu058_set_ann(hu058_display_t *disp, enum hu058_ann ann, bool on);
void hu058_set_ann_color(hu058_display_t *disp, enum hu058_ann ann, hu058_color_t color);
void hu058_set_brightness(hu058_display_t *disp, uint8_t level);
void hu058_set_time_format(hu058_display_t *disp, enum hu058_time_format fmt);
void hu058_set_time(hu058_display_t *disp, uint8_t hours, uint8_t minutes);
void hu058_set_temperature(hu058_display_t *disp, int16_t temp, bool fahrenheit);
void hu058_compile(hu058_display_t *disp);
void hu058_scan_step(hu058_display_t *disp);

#endif
