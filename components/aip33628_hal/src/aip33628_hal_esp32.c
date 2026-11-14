#include "aip33628_hal.h"
#include "driver/gpio.h"
#include "hal/gpio_ll.h"

/*
 * Everything on the send path is IRAM-resident and uses the inline
 * gpio_ll register writes rather than gpio_set_level(), because the scan
 * timer ISR keeps running while the flash cache is disabled.
 */

static inline void AIP33628_FAST_CODE bit_delay(void)
{
    for (volatile int i = 0; i < 4; i++) {
        __asm__ volatile("nop");
    }
}

static inline void AIP33628_FAST_CODE pin_write(int pin, int level)
{
    gpio_ll_set_level(&GPIO, (uint32_t)pin, (uint32_t)level);
}

void aip33628_hal_init(aip33628_hal_t *hal, int din_pin, int clk_pin)
{
    hal->din_pin = din_pin;
    hal->clk_pin = clk_pin;

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << din_pin) | (1ULL << clk_pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    gpio_set_level(din_pin, 0);
    gpio_set_level(clk_pin, 0);
}

void AIP33628_FAST_CODE aip33628_hal_send_frame(aip33628_hal_t *hal,
                                                uint32_t frame)
{
    int din = hal->din_pin;
    int clk = hal->clk_pin;

    // Clock out 30 data bits (bits 0-29), MSB first
    // AiP33628 frame: bits[15:0]=SEG, bits[23:16]=COM, bits[27:24]=current
    // Total 28 used bits but protocol sends 30
    for (int i = 29; i >= 0; i--) {
        pin_write(din, (frame >> i) & 1);
        bit_delay();
        pin_write(clk, 1);
        bit_delay();
        pin_write(clk, 0);
        bit_delay();
    }

    // Latch: DIN rising edge while CLK is high
    pin_write(din, 0);
    bit_delay();
    pin_write(clk, 1);
    bit_delay();
    pin_write(din, 1);
    bit_delay();
    pin_write(clk, 0);
    pin_write(din, 0);
}
