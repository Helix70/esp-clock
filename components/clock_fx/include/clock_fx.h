#ifndef CLOCK_FX_H
#define CLOCK_FX_H

#include "hu058.h"
#include <stdint.h>

/**
 * @file clock_fx.h
 * @brief Colour modes and effects for the HU-058 panel.
 *
 * Portable: writes per-LED colours into a hu058_display_t and leaves
 * compiling and scanning to the caller.
 */

typedef enum {
    CLOCK_FX_SOLID = 0,        /* colors[0] everywhere */
    CLOCK_FX_GRADIENT = 1,     /* colors[0] at the left edge to colors[1] at the right */
    CLOCK_FX_PER_DIGIT = 2,    /* colors[0..3], one per digit and its annunciators */
    CLOCK_FX_RAINBOW = 3,      /* whole panel cycles through the colour wheel */
    CLOCK_FX_RAINBOW_WAVE = 4, /* colour wheel sweeps left to right */
} clock_fx_mode_t;

/** True if the mode changes over time and needs regular re-application. */
bool clock_fx_is_animated(clock_fx_mode_t mode);

/**
 * @brief Colour every LED for the given mode.
 *
 * @param disp      Display to colour.
 * @param mode      Colour mode.
 * @param colors    Four 0xRRGGBB colours (meaning depends on mode).
 * @param period_s  Seconds per full trip round the colour wheel.
 * @param now_ms    Monotonic time in milliseconds, for animated modes.
 */
void clock_fx_apply(hu058_display_t *disp, clock_fx_mode_t mode,
                    const uint32_t colors[4], uint16_t period_s, uint32_t now_ms);

#endif
