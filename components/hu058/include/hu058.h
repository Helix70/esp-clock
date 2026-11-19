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
 * Colour depth comes from binary-weighted sub-frames: each COM pair holds
 * the bus for 15 ticks of HU058_UNIT_US, split 1+2+4+8, and a channel at
 * level L is lit in the sub-frames whose bit is set in L. That gives
 * 16 levels per channel (4096 colours) with no constraint between channels.
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
 *     // In a timer ISR every HU058_UNIT_US microseconds:
 *     hu058_scan_tick(&disp);
 */

/** Scan tick period. One full refresh is 4 x 15 ticks = 2.4 ms (417 Hz). */
#define HU058_UNIT_US 40

/** Colour channel levels run 0..HU058_LEVEL_MAX. */
#define HU058_LEVEL_MAX 15

/** Current (brightness) steps run 0..HU058_CURRENT_MAX. */
#define HU058_CURRENT_MAX 15

#define HU058_SUBFRAMES 4
#define HU058_MAX_STEPS (4 * HU058_SUBFRAMES)

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

/**
 * LED positions within one digit block: segments A-G, then the block's
 * annunciator, then a second annunciator (colon lower dot, block 1 only).
 */
enum hu058_led {
    HU058_LED_A = 0,
    HU058_LED_B,
    HU058_LED_C,
    HU058_LED_D,
    HU058_LED_E,
    HU058_LED_F,
    HU058_LED_G,
    HU058_LED_ANN,
    HU058_LED_ANN2,
    HU058_LEDS_PER_DIGIT
};

/** Colour as linear duty levels, 0..HU058_LEVEL_MAX per channel. */
typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} hu058_rgb_t;

#define HU058_RGB_WHITE ((hu058_rgb_t){HU058_LEVEL_MAX, HU058_LEVEL_MAX, HU058_LEVEL_MAX})
#define HU058_RGB_OFF   ((hu058_rgb_t){0, 0, 0})

/** One step of the scan schedule: latch this, then hold for @c units ticks. */
typedef struct {
    uint8_t units;
    uint8_t com;
    uint16_t seg[2];
} hu058_step_t;

/** A complete scan pattern, built by hu058_compile(). */
typedef struct {
    hu058_step_t step[HU058_MAX_STEPS];
    uint8_t n;
    uint8_t current;
} hu058_sched_t;

/** Display state. */
typedef struct {
    aip33628_t *drv[2];

    uint8_t digits[HU058_NUM_DIGITS];
    bool ann[HU058_NUM_ANN];
    hu058_rgb_t color[HU058_NUM_DIGITS][HU058_LEDS_PER_DIGIT];

    enum hu058_time_format time_format;
    uint8_t current;

    /* Double-buffered schedule: compile fills the back one, then flips. */
    hu058_sched_t sched[2];
    volatile uint8_t front;
    volatile uint8_t step;
    volatile uint8_t wait;
} hu058_display_t;

void hu058_init(hu058_display_t *disp, aip33628_t *drv_h, aip33628_t *drv_m);

/** Blank all digits and annunciators (colours are kept). */
void hu058_clear(hu058_display_t *disp);

void hu058_set_digit(hu058_display_t *disp, enum hu058_pos pos, uint8_t value);
void hu058_set_segments(hu058_display_t *disp, enum hu058_pos pos, uint8_t segs);

/**
 * @brief Draw one character. Letters that only exist in the other case
 *        fall back to it; characters with no glyph (K M V W X Z) draw blank.
 */
void hu058_set_char(hu058_display_t *disp, enum hu058_pos pos, char c);

/** @brief Draw up to four characters, left aligned, padding with blanks. */
void hu058_set_text(hu058_display_t *disp, const char *text);

/** @brief Seven-segment bitmask for a character, 0 if it has no glyph. */
uint8_t hu058_glyph(char c);

void hu058_set_ann(hu058_display_t *disp, enum hu058_ann ann, bool on);

/** Colour of one LED, by digit block and position within it. */
void hu058_set_led_color(hu058_display_t *disp, int digit, enum hu058_led led,
                         hu058_rgb_t color);
/** Colour of a digit's seven segments. */
void hu058_set_digit_color(hu058_display_t *disp, enum hu058_pos pos, hu058_rgb_t color);
void hu058_set_ann_color(hu058_display_t *disp, enum hu058_ann ann, hu058_rgb_t color);
/** Colour of every LED on the panel. */
void hu058_set_all_color(hu058_display_t *disp, hu058_rgb_t color);

/**
 * @brief Physical position of an LED, normalised to 0..1 across the panel
 *        (x left to right, y top to bottom).
 * @return false if nothing is wired at that position.
 */
bool hu058_led_xy(int digit, enum hu058_led led, float *x, float *y);

/** Drive current step, 0..HU058_CURRENT_MAX (2.5 mA to 40.4 mA per LED). */
void hu058_set_brightness(hu058_display_t *disp, uint8_t level);

/** Convert a gamma-encoded 0xRRGGBB colour to linear duty levels. */
hu058_rgb_t hu058_rgb_from_888(uint32_t rgb);

void hu058_set_time_format(hu058_display_t *disp, enum hu058_time_format fmt);

/**
 * @brief Draw a time. 24-hour keeps the leading zero (07:30), 12-hour
 *        drops it (7:30) and lights AM before noon. Leaves the colon alone.
 */
void hu058_set_time(hu058_display_t *disp, uint8_t hours, uint8_t minutes);

/**
 * @brief Draw a whole-degree temperature with the degree mark and a C/F
 *        unit: three digits when positive, two and a minus when negative.
 *        Clamped to -99..999.
 */
void hu058_set_temperature(hu058_display_t *disp, int16_t temp, bool fahrenheit);

/** Draw two two-digit numbers with the date dash between them. */
void hu058_set_date(hu058_display_t *disp, uint8_t first, uint8_t second);

/** Build the scan schedule from the current state and publish it. */
void hu058_compile(hu058_display_t *disp);

/** Advance the scan. Call every HU058_UNIT_US from a timer ISR. */
void hu058_scan_tick(hu058_display_t *disp);

#endif
