#ifndef AIP33628_H
#define AIP33628_H

#include "aip33628_hal.h"
#include <stdint.h>

typedef struct {
    aip33628_hal_t hal;
} aip33628_t;

/** Number of current steps (IS field). */
#define AIP33628_CURRENT_STEPS 16

/** Per-sink current for each IS step, in tenths of a mA (datasheet). */
extern const uint16_t AIP33628_CURRENT_TENTHS_MA[AIP33628_CURRENT_STEPS];

void aip33628_init(aip33628_t *dev, int din_pin, int clk_pin);
void aip33628_write(aip33628_t *dev, uint16_t segments, uint8_t commons, uint8_t current);
void aip33628_write_raw(aip33628_t *dev, uint32_t frame);

/**
 * @brief Write one frame to each of two drivers at once.
 *
 * COM and current are shared; only the segment words differ. Both drivers
 * latch together.
 */
void aip33628_write_pair(aip33628_t *a, aip33628_t *b, uint16_t segments_a,
                         uint16_t segments_b, uint8_t commons, uint8_t current);

#endif
