#include "aip33628_hal.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "soc/gpio_reg.h"
#include "soc/soc.h"

/*
 * Everything on the send path is IRAM-resident and writes the GPIO set/clear
 * registers directly, because the scan timer ISR keeps running while the
 * flash cache is disabled. Each register store takes longer than the 16 ns
 * CLK high/low time the AiP33628 needs, so no extra padding is required.
 */
#define PORT_SET(m) REG_WRITE(GPIO_OUT_W1TS_REG, (m))
#define PORT_CLR(m) REG_WRITE(GPIO_OUT_W1TC_REG, (m))

static const char *TAG = "aip33628_hal";

void aip33628_hal_init(aip33628_hal_t *hal, int din_pin, int clk_pin)
{
    /* The port registers cover GPIO0-31 only. */
    if (din_pin < 0 || din_pin > 31 || clk_pin < 0 || clk_pin > 31) {
        ESP_LOGE(TAG, "Display pins must be GPIO0-31 (DIN %d, CLK %d)",
                 din_pin, clk_pin);
        abort();
    }

    hal->din_pin = din_pin;
    hal->clk_pin = clk_pin;
    hal->din_mask = 1u << din_pin;
    hal->clk_mask = 1u << clk_pin;

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

/*
 * Shift both frames out bit 0 first. Data changes only while CLK is low and
 * is shifted on the CLK rising edge. After bit 29 CLK stays high and a DIN
 * rising edge latches the frame, which is the sequence the stock HU-058
 * firmware produces. Dropping CLK and raising it again before the latch
 * would clock in a 31st bit.
 */
static inline void AIP33628_FAST_CODE send_masks(uint32_t frame_a, uint32_t frame_b,
                                                 uint32_t din_a, uint32_t din_b,
                                                 uint32_t clks)
{
    const uint32_t dins = din_a | din_b;

    PORT_CLR(clks | dins);

    for (int i = 0; i < 30; i++) {
        uint32_t set = 0;
        if (frame_a & 1u) set |= din_a;
        if (frame_b & 1u) set |= din_b;
        frame_a >>= 1;
        frame_b >>= 1;

        PORT_CLR(dins & ~set);
        PORT_SET(set);
        PORT_SET(clks);
        if (i < 29) PORT_CLR(clks);
    }

    /* CLK is still high after bit 29: DIN low then high is the latch. */
    PORT_CLR(dins);
    PORT_SET(dins);
    PORT_CLR(dins);
    PORT_CLR(clks);
}

void AIP33628_FAST_CODE aip33628_hal_send_frame(aip33628_hal_t *hal,
                                                uint32_t frame)
{
    send_masks(frame, 0, hal->din_mask, 0, hal->clk_mask);
}

void AIP33628_FAST_CODE aip33628_hal_send_pair(aip33628_hal_t *a,
                                               aip33628_hal_t *b,
                                               uint32_t frame_a,
                                               uint32_t frame_b)
{
    send_masks(frame_a, frame_b, a->din_mask, b->din_mask,
               a->clk_mask | b->clk_mask);
}
