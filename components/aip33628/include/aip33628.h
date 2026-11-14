#ifndef AIP33628_H
#define AIP33628_H

#include "aip33628_hal.h"
#include <stdint.h>

typedef struct {
    aip33628_hal_t hal;
} aip33628_t;

void aip33628_init(aip33628_t *dev, int din_pin, int clk_pin);
void aip33628_write(aip33628_t *dev, uint16_t segments, uint8_t commons, uint8_t current);
void aip33628_write_raw(aip33628_t *dev, uint32_t frame);

#endif
