#ifndef AIP33628_HAL_H
#define AIP33628_HAL_H

#include <stdint.h>

/*
 * Attribute applied to every function on the display scan path. On ESP-IDF
 * the scan runs from a GPTimer ISR, which keeps firing while the flash cache
 * is disabled (NVS writes, OTA), so that code must live in IRAM. Other
 * platforms can leave it empty.
 */
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define AIP33628_FAST_CODE IRAM_ATTR
#define AIP33628_FAST_DATA DRAM_ATTR
#else
#define AIP33628_FAST_CODE
#define AIP33628_FAST_DATA
#endif

typedef struct {
    int din_pin;
    int clk_pin;
    uint32_t din_mask;
    uint32_t clk_mask;
} aip33628_hal_t;

void aip33628_hal_init(aip33628_hal_t *hal, int din_pin, int clk_pin);

/*
 * Clock one 30-bit frame out, bit 0 first, then latch it.
 * Definition carries AIP33628_FAST_CODE (attribute on prototype too would
 * produce a conflicting section name under GCC).
 */
void aip33628_hal_send_frame(aip33628_hal_t *hal, uint32_t frame);

/*
 * Clock one frame into each of two drivers on separate buses at the same
 * time, then latch both. Halves the time a full display update takes, which
 * matters because the shortest colour sub-frame is only 40 us.
 */
void aip33628_hal_send_pair(aip33628_hal_t *a, aip33628_hal_t *b,
                            uint32_t frame_a, uint32_t frame_b);

#endif
