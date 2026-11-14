/**
 * @file aip33628.c
 * @brief Portable AiP33628 30-bit frame packing and bit-reversal logic.
 */
#include "aip33628.h"

/**
 * @brief Reverse the bottom @p n bits of @p v.
 */
static uint32_t AIP33628_FAST_CODE reverse_bits(uint32_t v, unsigned n)
{
    uint32_t r = 0;
    for (unsigned i = 0; i < n; i++) {
        r |= ((v >> i) & 1u) << (n - 1 - i);
    }
    return r;
}

/**
 * @brief Reverse the 4-bit current field per the AiP33628 datasheet.
 */
static uint8_t AIP33628_FAST_CODE reverse_current(uint8_t current)
{
    return (uint8_t)reverse_bits(current & 0x0Fu, 4);
}

void aip33628_init(aip33628_t *dev, int din_pin, int clk_pin)
{
    aip33628_hal_init(&dev->hal, din_pin, clk_pin);
}

void AIP33628_FAST_CODE aip33628_write(aip33628_t *dev, uint16_t segments,
                                       uint8_t commons, uint8_t current)
{
    /*
     * Pack the 30-bit frame in the spec's LSB-first layout:
     *   Bit  0-15  SS[15:0]  segment on/off
     *   Bit 16-23  CS[7:0]   common anode drivers
     *   Bit 24-27  IS[3:0]   current level (bit-reversed on wire)
     *   Bit 28-29  reserved  must be 0
     */
    uint32_t frame = (uint32_t)(segments & 0xFFFFu)
                   | ((uint32_t)(commons & 0xFFu) << 16)
                   | ((uint32_t)reverse_current(current) << 24);

    aip33628_write_raw(dev, frame);
}

void AIP33628_FAST_CODE aip33628_write_raw(aip33628_t *dev, uint32_t frame)
{
    uint32_t word = reverse_bits(frame & 0x3FFFFFFFu, 30);
    aip33628_hal_send_frame(&dev->hal, word);
}
