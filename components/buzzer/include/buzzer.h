#ifndef BUZZER_H
#define BUZZER_H

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @file buzzer.h
 * @brief HU-058 buzzer (LS1 via PNP Q1), driven with an LEDC square wave.
 *
 * The board's driver is active low: pulling R_B low turns on Q1 and sounds
 * the buzzer. Q1's emitter is at 5V, so the GPIO drives an NPN that pulls
 * R_B low instead: the pin is push-pull, high = sound, idle low.
 *
 * Playback runs in its own task, so every call returns immediately. A new
 * call replaces whatever is playing.
 */

/** One note of a sequence. freq_hz 0 is a rest. */
typedef struct {
    uint16_t freq_hz;
    uint16_t ms;
} buzzer_note_t;

/** Longest sequence buzzer_play() accepts. */
#define BUZZER_MAX_NOTES 16

/** Loudest frequency for the 12 mm magnetic buzzers these kits ship with. */
#define BUZZER_DEFAULT_HZ 2700

esp_err_t buzzer_init(int gpio);

/** Play a single tone for @p ms milliseconds. */
void buzzer_beep(uint16_t freq_hz, uint16_t ms);

/** Play a sequence of up to BUZZER_MAX_NOTES notes (copied). */
void buzzer_play(const buzzer_note_t *notes, size_t count);

/** Silence the buzzer now. */
void buzzer_stop(void);

#endif
