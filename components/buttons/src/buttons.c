#include "buttons.h"
#include <stdint.h>

#define DEBOUNCE_SAMPLES  3
#define HOLD_TICKS        (2000 / 20)  /* 2 seconds at 20ms poll */

static button_callback_t cb;

static uint8_t raw1_hist;
static uint8_t raw2_hist;
static bool stable1;
static bool stable2;
static uint32_t both_held_ticks;
static bool both_held_fired;
static uint32_t btn1_held_ticks;
static bool btn1_held_fired;

static bool debounce(uint8_t *hist, bool sample)
{
    *hist = ((*hist << 1) | (sample ? 1 : 0)) & ((1 << DEBOUNCE_SAMPLES) - 1);
    uint8_t mask = (1 << DEBOUNCE_SAMPLES) - 1;
    if (*hist == mask) return true;
    if (*hist == 0)    return false;
    return sample;
}

void buttons_init(button_callback_t callback)
{
    cb = callback;
    raw1_hist = 0;
    raw2_hist = 0;
    stable1 = false;
    stable2 = false;
    both_held_ticks = 0;
    both_held_fired = false;
    btn1_held_ticks = 0;
    btn1_held_fired = false;
}

void buttons_poll(bool btn1_pressed, bool btn2_pressed)
{
    bool new1 = debounce(&raw1_hist, btn1_pressed);
    bool new2 = debounce(&raw2_hist, btn2_pressed);

    if (new1 != stable1) {
        stable1 = new1;
        if (cb) cb(new1 ? BUTTON_1_PRESS : BUTTON_1_RELEASE);
    }

    if (new2 != stable2) {
        stable2 = new2;
        if (cb) cb(new2 ? BUTTON_2_PRESS : BUTTON_2_RELEASE);
    }

    if (stable1 && stable2) {
        both_held_ticks++;
        if (both_held_ticks >= HOLD_TICKS && !both_held_fired) {
            both_held_fired = true;
            if (cb) cb(BUTTON_BOTH_HELD);
        }
        btn1_held_ticks = 0;
        btn1_held_fired = false;
    } else {
        both_held_ticks = 0;
        both_held_fired = false;
        if (stable1 && !stable2) {
            btn1_held_ticks++;
            if (btn1_held_ticks >= HOLD_TICKS && !btn1_held_fired) {
                btn1_held_fired = true;
                if (cb) cb(BUTTON_1_HELD);
            }
        } else {
            btn1_held_ticks = 0;
            btn1_held_fired = false;
        }
    }
}
