/**
 * @file aip33628.c
 * @brief Portable AiP33628 30-bit frame packing.
 */
#include "aip33628.h"

const uint16_t AIP33628_CURRENT_TENTHS_MA[AIP33628_CURRENT_STEPS] = {
    25, 51, 76, 101, 126, 152, 177, 202,
    227, 253, 278, 303, 328, 354, 379, 404,
};

/*
 * Current step to the nibble that goes in bits 24-27. The frame carries
 * IS[0] at bit 27 and IS[3] at bit 24, so the field is bit-reversed against
 * the rest of the frame. Steps 0 and 15 are palindromes, which is why getting
 * this wrong looks right at both ends of the range.
 */
static const uint8_t AIP33628_FAST_DATA IS_WIRE[16] = {
    0x0, 0x8, 0x4, 0xC, 0x2, 0xA, 0x6, 0xE,
    0x1, 0x9, 0x5, 0xD, 0x3, 0xB, 0x7, 0xF,
};

/*
 * Pack the 30-bit frame in the spec's LSB-first layout:
 *   Bit  0-15  SS[15:0]  segment on/off
 *   Bit 16-23  CS[7:0]   common anode drivers
 *   Bit 24-27  IS[3:0]   current level (bit-reversed)
 *   Bit 28-29  reserved  must be 0
 */
static inline uint32_t AIP33628_FAST_CODE pack(uint16_t segments,
                                              uint8_t commons,
                                              uint8_t current)
{
    return (uint32_t)segments
         | ((uint32_t)commons << 16)
         | ((uint32_t)IS_WIRE[current & 0x0Fu] << 24);
}

void aip33628_init(aip33628_t *dev, int din_pin, int clk_pin)
{
    aip33628_hal_init(&dev->hal, din_pin, clk_pin);
}

void AIP33628_FAST_CODE aip33628_write(aip33628_t *dev, uint16_t segments,
                                       uint8_t commons, uint8_t current)
{
    aip33628_hal_send_frame(&dev->hal, pack(segments, commons, current));
}

void AIP33628_FAST_CODE aip33628_write_raw(aip33628_t *dev, uint32_t frame)
{
    aip33628_hal_send_frame(&dev->hal, frame & 0x3FFFFFFFu);
}

void AIP33628_FAST_CODE aip33628_write_pair(aip33628_t *a, aip33628_t *b,
                                            uint16_t segments_a,
                                            uint16_t segments_b,
                                            uint8_t commons, uint8_t current)
{
    aip33628_hal_send_pair(&a->hal, &b->hal,
                           pack(segments_a, commons, current),
                           pack(segments_b, commons, current));
}
