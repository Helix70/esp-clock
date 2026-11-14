#ifndef AIP33628_HAL_H
#define AIP33628_HAL_H

#include <stdint.h>

/*
 * Attribute applied to every function on the display scan path. On ESP-IDF
 * the scan runs from an ISR-dispatched esp_timer, which keeps firing while
 * the flash cache is disabled (NVS/WiFi writes), so that code must live in
 * IRAM. Other platforms can leave it empty.
 */
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define AIP33628_FAST_CODE IRAM_ATTR
#else
#define AIP33628_FAST_CODE
#endif

typedef struct {
    int din_pin;
    int clk_pin;
} aip33628_hal_t;

void aip33628_hal_init(aip33628_hal_t *hal, int din_pin, int clk_pin);
/* Definition carries AIP33628_FAST_CODE (attribute on prototype too would
 * produce a conflicting section name under GCC). */
void aip33628_hal_send_frame(aip33628_hal_t *hal, uint32_t frame);

#endif
