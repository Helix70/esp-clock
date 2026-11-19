#include "clock_fx.h"
#include <math.h>

/* How much of the colour wheel the wave spreads across the panel. A full
 * turn over four digits packs the spectrum into a hand span and reads as
 * noise, so a slice looks better. */
#define WAVE_SPAN 0.35f

bool clock_fx_is_animated(clock_fx_mode_t mode)
{
    return mode == CLOCK_FX_RAINBOW || mode == CLOCK_FX_RAINBOW_WAVE;
}

/* Fully saturated hue (0..1, wrapping) to 0xRRGGBB. */
static uint32_t hue_888(float h)
{
    h -= floorf(h);
    float x = h * 6.0f;
    int i = (int)x;
    float f = x - (float)i;
    float r, g, b;

    switch (i % 6) {
    case 0:  r = 1.0f;     g = f;        b = 0.0f;     break;
    case 1:  r = 1.0f - f; g = 1.0f;     b = 0.0f;     break;
    case 2:  r = 0.0f;     g = 1.0f;     b = f;        break;
    case 3:  r = 0.0f;     g = 1.0f - f; b = 1.0f;     break;
    case 4:  r = f;        g = 0.0f;     b = 1.0f;     break;
    default: r = 1.0f;     g = 0.0f;     b = 1.0f - f; break;
    }

    return ((uint32_t)lroundf(r * 255) << 16) |
           ((uint32_t)lroundf(g * 255) << 8) |
           (uint32_t)lroundf(b * 255);
}

static uint32_t lerp_888(uint32_t a, uint32_t b, float t)
{
    uint32_t out = 0;
    for (int shift = 0; shift <= 16; shift += 8) {
        float ca = (float)((a >> shift) & 0xFF);
        float cb = (float)((b >> shift) & 0xFF);
        out |= (uint32_t)lroundf(ca + (cb - ca) * t) << shift;
    }
    return out;
}

void clock_fx_apply(hu058_display_t *disp, clock_fx_mode_t mode,
                    const uint32_t colors[4], uint16_t period_s, uint32_t now_ms)
{
    if (period_s == 0) period_s = 1;
    float phase = (float)(now_ms % ((uint32_t)period_s * 1000u)) /
                  ((float)period_s * 1000.0f);

    switch (mode) {
    case CLOCK_FX_SOLID:
        hu058_set_all_color(disp, hu058_rgb_from_888(colors[0]));
        return;

    case CLOCK_FX_RAINBOW:
        hu058_set_all_color(disp, hu058_rgb_from_888(hue_888(phase)));
        return;

    case CLOCK_FX_PER_DIGIT:
        for (int d = 0; d < HU058_NUM_DIGITS; d++) {
            hu058_rgb_t c = hu058_rgb_from_888(colors[d]);
            for (int led = 0; led < HU058_LEDS_PER_DIGIT; led++)
                hu058_set_led_color(disp, d, led, c);
        }
        return;

    case CLOCK_FX_GRADIENT:
    case CLOCK_FX_RAINBOW_WAVE:
    default:
        for (int d = 0; d < HU058_NUM_DIGITS; d++) {
            for (int led = 0; led < HU058_LEDS_PER_DIGIT; led++) {
                float x, y;
                if (!hu058_led_xy(d, led, &x, &y)) continue;
                uint32_t rgb = (mode == CLOCK_FX_RAINBOW_WAVE)
                    ? hue_888(phase + x * WAVE_SPAN)
                    : lerp_888(colors[0], colors[1], x);
                hu058_set_led_color(disp, d, led, hu058_rgb_from_888(rgb));
            }
        }
        return;
    }
}
