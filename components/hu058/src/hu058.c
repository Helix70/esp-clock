#include "hu058.h"
#include <math.h>
#include <string.h>

/* Order the COM pairs are scanned in; index = digit block * 2 + side. */
static const uint8_t COM_SEQ[4] = {0x30, 0xC0, 0x03, 0x0C};

/*
 * Where each LED position sits on its driver. Side 0 is the block's low COM
 * pair, side 1 the high one; led is the RGB LED number (1-5) on SEG1-SEG15.
 */
static const struct {
    uint8_t side;
    uint8_t led;
} SEGMAP[HU058_LEDS_PER_DIGIT] = {
    [HU058_LED_A]    = {1, 5},
    [HU058_LED_B]    = {1, 4},
    [HU058_LED_C]    = {1, 3},
    [HU058_LED_D]    = {1, 2},
    [HU058_LED_E]    = {0, 5},
    [HU058_LED_F]    = {0, 4},
    [HU058_LED_G]    = {0, 3},
    [HU058_LED_ANN]  = {1, 1},
    [HU058_LED_ANN2] = {0, 1},
};

/* Annunciator to (digit block, LED position). */
static const struct {
    uint8_t digit;
    uint8_t led;
} ANN_MAP[HU058_NUM_ANN] = {
    [HU058_ANN_AM]          = {0, HU058_LED_ANN},
    [HU058_ANN_COLON_UPPER] = {1, HU058_LED_ANN},
    [HU058_ANN_COLON_LOWER] = {1, HU058_LED_ANN2},
    [HU058_ANN_DASH]        = {2, HU058_LED_ANN},
    [HU058_ANN_DEGREE]      = {3, HU058_LED_ANN},
};

/* Seven-segment font, bit 0 = A through bit 6 = G. */
static uint8_t glyph_exact(char c)
{
    switch (c) {
    case '0': return 0x3F;
    case '1': return 0x06;
    case '2': return 0x5B;
    case '3': return 0x4F;
    case '4': return 0x66;
    case '5': return 0x6D;
    case '6': return 0x7D;
    case '7': return 0x07;
    case '8': return 0x7F;
    case '9': return 0x6F;
    case 'A': return 0x77;
    case 'b': return 0x7C;
    case 'C': return 0x39;
    case 'c': return 0x58;
    case 'd': return 0x5E;
    case 'E': return 0x79;
    case 'F': return 0x71;
    case 'G': return 0x3D;
    case 'g': return 0x6F;
    case 'H': return 0x76;
    case 'h': return 0x74;
    case 'I': return 0x30; /* left side, so it does not read as a 1 */
    case 'i': return 0x10;
    case 'J': return 0x1E;
    case 'L': return 0x38;
    case 'l': return 0x30;
    case 'N': return 0x37;
    case 'n': return 0x54;
    case 'O': return 0x3F;
    case 'o': return 0x5C;
    case 'P': return 0x73;
    case 'q': return 0x67;
    case 'r': return 0x50;
    case 'S': return 0x6D;
    case 't': return 0x78;
    case 'U': return 0x3E;
    case 'u': return 0x1C;
    case 'y': return 0x6E;
    case '-': return 0x40;
    case '_': return 0x08;
    case '=': return 0x48;
    case '\'': return 0x02;
    case '"': return 0x22;
    case '*': return 0x63; /* degree sign */
    default: return 0;
    }
}

uint8_t hu058_glyph(char c)
{
    uint8_t g = glyph_exact(c);
    if (g == 0 && c >= 'a' && c <= 'z')
        g = glyph_exact((char)(c - 'a' + 'A'));
    else if (g == 0 && c >= 'A' && c <= 'Z')
        g = glyph_exact((char)(c - 'A' + 'a'));
    return g;
}

void hu058_init(hu058_display_t *disp, aip33628_t *drv_h, aip33628_t *drv_m)
{
    memset(disp, 0, sizeof(*disp));

    disp->drv[0] = drv_h;
    disp->drv[1] = drv_m;
    disp->current = 8;

    hu058_set_all_color(disp, HU058_RGB_WHITE);
    hu058_compile(disp);
}

void hu058_clear(hu058_display_t *disp)
{
    memset(disp->digits, 0, sizeof(disp->digits));
    memset(disp->ann, 0, sizeof(disp->ann));
}

void hu058_set_digit(hu058_display_t *disp, enum hu058_pos pos, uint8_t value)
{
    disp->digits[pos] = (value < 10) ? glyph_exact((char)('0' + value)) : 0;
}

void hu058_set_segments(hu058_display_t *disp, enum hu058_pos pos, uint8_t segs)
{
    disp->digits[pos] = segs & 0x7F;
}

void hu058_set_char(hu058_display_t *disp, enum hu058_pos pos, char c)
{
    disp->digits[pos] = hu058_glyph(c);
}

void hu058_set_text(hu058_display_t *disp, const char *text)
{
    for (int i = 0; i < HU058_NUM_DIGITS; i++) {
        char c = *text ? *text++ : ' ';
        hu058_set_char(disp, i, c);
    }
}

void hu058_set_ann(hu058_display_t *disp, enum hu058_ann ann, bool on)
{
    disp->ann[ann] = on;
}

void hu058_set_led_color(hu058_display_t *disp, int digit, enum hu058_led led,
                         hu058_rgb_t color)
{
    disp->color[digit][led] = color;
}

void hu058_set_digit_color(hu058_display_t *disp, enum hu058_pos pos,
                           hu058_rgb_t color)
{
    for (int led = HU058_LED_A; led <= HU058_LED_G; led++)
        disp->color[pos][led] = color;
}

void hu058_set_ann_color(hu058_display_t *disp, enum hu058_ann ann,
                         hu058_rgb_t color)
{
    disp->color[ANN_MAP[ann].digit][ANN_MAP[ann].led] = color;
}

void hu058_set_all_color(hu058_display_t *disp, hu058_rgb_t color)
{
    for (int d = 0; d < HU058_NUM_DIGITS; d++)
        for (int led = 0; led < HU058_LEDS_PER_DIGIT; led++)
            disp->color[d][led] = color;
}

/*
 * Approximate panel geometry in digit widths. Digits are one unit wide and
 * one tall, with a wider gap at the colon. Normalised to 0..1 on output.
 */
#define PANEL_WIDTH 6.0f
static const float DIGIT_X[HU058_NUM_DIGITS] = {0.0f, 1.6f, 3.4f, 5.0f};
static const float SEG_XY[7][2] = {
    {0.5f, 0.0f},  /* A */
    {1.0f, 0.25f}, /* B */
    {1.0f, 0.75f}, /* C */
    {0.5f, 1.0f},  /* D */
    {0.0f, 0.75f}, /* E */
    {0.0f, 0.25f}, /* F */
    {0.5f, 0.5f},  /* G */
};

bool hu058_led_xy(int digit, enum hu058_led led, float *x, float *y)
{
    float px, py;

    if (led <= HU058_LED_G) {
        px = DIGIT_X[digit] + SEG_XY[led][0];
        py = SEG_XY[led][1];
    } else if (led == HU058_LED_ANN2) {
        if (digit != 1) return false;
        px = 3.0f; py = 0.7f;                       /* colon lower */
    } else {
        switch (digit) {
        case 0:  px = 0.5f; py = 0.25f; break;      /* AM */
        case 1:  px = 3.0f; py = 0.3f;  break;      /* colon upper */
        case 2:  px = 3.0f; py = 0.5f;  break;      /* date dash */
        default: px = 4.8f; py = 0.05f; break;      /* degree */
        }
    }

    *x = px / PANEL_WIDTH;
    *y = py;
    return true;
}

void hu058_set_brightness(hu058_display_t *disp, uint8_t level)
{
    disp->current = (level > HU058_CURRENT_MAX) ? HU058_CURRENT_MAX : level;
}

/* Exponent used to linearise colour: duty is linear light, while colours
 * from a colour picker are gamma encoded. */
#define PANEL_GAMMA 2.8f

static uint8_t level_from_8bit(uint8_t c)
{
    return (uint8_t)lroundf(powf(c / 255.0f, PANEL_GAMMA) * HU058_LEVEL_MAX);
}

hu058_rgb_t hu058_rgb_from_888(uint32_t rgb)
{
    uint8_t r8 = (rgb >> 16) & 0xFF, g8 = (rgb >> 8) & 0xFF, b8 = rgb & 0xFF;
    hu058_rgb_t c = {level_from_8bit(r8), level_from_8bit(g8), level_from_8bit(b8)};

    /* A dark colour must not round away to nothing: keep its strongest
     * channel at the lowest visible level. */
    if (c.r == 0 && c.g == 0 && c.b == 0) {
        if (r8 && r8 >= g8 && r8 >= b8) c.r = 1;
        else if (g8 && g8 >= b8) c.g = 1;
        else if (b8) c.b = 1;
    }
    return c;
}

void hu058_set_time_format(hu058_display_t *disp, enum hu058_time_format fmt)
{
    disp->time_format = fmt;
}

void hu058_set_time(hu058_display_t *disp, uint8_t hours, uint8_t minutes)
{
    bool twelve = disp->time_format == HU058_12H;

    disp->ann[HU058_ANN_AM] = twelve && hours < 12;
    if (twelve) {
        hours %= 12;
        if (hours == 0) hours = 12;
    }

    disp->digits[HU058_H_TENS] =
        (twelve && hours < 10) ? 0 : glyph_exact((char)('0' + hours / 10));
    hu058_set_digit(disp, HU058_H_ONES, hours % 10);
    hu058_set_digit(disp, HU058_M_TENS, minutes / 10);
    hu058_set_digit(disp, HU058_M_ONES, minutes % 10);
}

void hu058_set_temperature(hu058_display_t *disp, int16_t temp, bool fahrenheit)
{
    if (temp < -99) temp = -99;
    if (temp > 999) temp = 999;

    disp->ann[HU058_ANN_AM] = false;
    disp->ann[HU058_ANN_DEGREE] = true;
    hu058_set_char(disp, HU058_M_ONES, fahrenheit ? 'F' : 'C');

    bool negative = (temp < 0);
    uint16_t abs_temp = negative ? (uint16_t)(-temp) : (uint16_t)temp;

    uint8_t ones = abs_temp % 10;
    uint8_t tens = (abs_temp / 10) % 10;
    uint8_t hundreds = (abs_temp / 100) % 10;

    hu058_set_digit(disp, HU058_M_TENS, ones);

    if (negative) {
        if (tens > 0) {
            hu058_set_digit(disp, HU058_H_ONES, tens);
            hu058_set_char(disp, HU058_H_TENS, '-');
        } else {
            hu058_set_char(disp, HU058_H_ONES, '-');
            disp->digits[HU058_H_TENS] = 0;
        }
    } else {
        if (hundreds > 0 || tens > 0)
            hu058_set_digit(disp, HU058_H_ONES, tens);
        else
            disp->digits[HU058_H_ONES] = 0;
        if (hundreds > 0)
            hu058_set_digit(disp, HU058_H_TENS, hundreds);
        else
            disp->digits[HU058_H_TENS] = 0;
    }
}

void hu058_set_date(hu058_display_t *disp, uint8_t first, uint8_t second)
{
    hu058_set_digit(disp, HU058_H_TENS, (first / 10) % 10);
    hu058_set_digit(disp, HU058_H_ONES, first % 10);
    hu058_set_digit(disp, HU058_M_TENS, (second / 10) % 10);
    hu058_set_digit(disp, HU058_M_ONES, second % 10);
    disp->ann[HU058_ANN_AM] = false;
    disp->ann[HU058_ANN_DASH] = true;
}

static bool led_lit(const hu058_display_t *disp, int digit, int led)
{
    if (led <= HU058_LED_G)
        return (disp->digits[digit] >> led) & 1;

    for (int a = 0; a < HU058_NUM_ANN; a++) {
        if (ANN_MAP[a].digit == digit && ANN_MAP[a].led == led)
            return disp->ann[a];
    }
    return false;
}

void hu058_compile(hu058_display_t *disp)
{
    /* sub[driver][com index][sub-frame] */
    uint16_t sub[2][4][HU058_SUBFRAMES];
    memset(sub, 0, sizeof(sub));

    for (int d = 0; d < HU058_NUM_DIGITS; d++) {
        int drv = d >> 1;
        int block = d & 1;

        for (int led = 0; led < HU058_LEDS_PER_DIGIT; led++) {
            if (!led_lit(disp, d, led)) continue;

            int ci = block * 2 + SEGMAP[led].side;
            int base = (SEGMAP[led].led - 1) * 3 + 1; /* SEG lines B, G, R */
            const hu058_rgb_t c = disp->color[d][led];
            const uint8_t lv[3] = {c.b, c.g, c.r};

            for (int ch = 0; ch < 3; ch++) {
                uint16_t bit = (uint16_t)(1u << (base + ch));
                for (int k = 0; k < HU058_SUBFRAMES; k++) {
                    if (lv[ch] & (1u << k))
                        sub[drv][ci][k] |= bit;
                }
            }
        }
    }

    hu058_sched_t *s = &disp->sched[disp->front ^ 1];
    s->n = 0;
    for (int ci = 0; ci < 4; ci++) {
        for (int k = 0; k < HU058_SUBFRAMES; k++) {
            uint16_t s0 = sub[0][ci][k];
            uint16_t s1 = sub[1][ci][k];
            uint8_t units = (uint8_t)(1u << k);

            /* Identical neighbours within one COM pair merge into a longer
             * step, so a saturated colour costs one send per COM pair. */
            if (k > 0 && s->step[s->n - 1].seg[0] == s0 &&
                s->step[s->n - 1].seg[1] == s1) {
                s->step[s->n - 1].units += units;
                continue;
            }

            hu058_step_t *st = &s->step[s->n++];
            st->units = units;
            st->com = COM_SEQ[ci];
            st->seg[0] = s0;
            st->seg[1] = s1;
        }
    }
    s->current = disp->current;

    /* Publish. The barrier keeps the writes above from being reordered past
     * the flip, so the scan ISR never sees a half-built schedule. */
    __sync_synchronize();
    disp->front ^= 1;
}

/*
 * Most ticks only count down. A fixed auto-reload tick is used rather than
 * reprogramming the alarm per step, so a late interrupt costs one uneven
 * sub-frame instead of a missed alarm.
 */
void AIP33628_FAST_CODE hu058_scan_tick(hu058_display_t *disp)
{
    if (disp->wait > 0) {
        disp->wait--;
        return;
    }

    const hu058_sched_t *s = &disp->sched[disp->front];

    /* The schedule may have been swapped for a shorter one mid-cycle. */
    uint8_t i = disp->step;
    if (i >= s->n) i = 0;
    const hu058_step_t *st = &s->step[i];

    aip33628_write_pair(disp->drv[0], disp->drv[1], st->seg[0], st->seg[1],
                        st->com, s->current);

    disp->wait = (uint8_t)(st->units - 1);
    disp->step = (uint8_t)((i + 1 >= s->n) ? 0 : i + 1);
}
