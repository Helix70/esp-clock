#include "ds1302.h"

#include "driver/gpio.h"
#include "esp_rom_sys.h"

/* Command byte: bit 7 always 1, bit 6 RAM/clock, bits 5-1 address,
 * bit 0 read/write. */
#define CMD_BASE        0x80
#define CMD_RAM         0x40
#define CMD_READ        0x01

#define REG_SECONDS     0
#define REG_WP          7
#define REG_TRICKLE     8

#define REG_BURST       31   /* address 31 selects burst mode */

#define SECONDS_CH      0x80 /* clock halt */
#define HOURS_12H       0x80
#define HOURS_PM        0x20
#define WP_ENABLE       0x80
#define TRICKLE_OFF     0x00 /* anything other than 1010xxxx disables it */

/* The DS1302 runs at 500 kHz worst case (2 V); 1 us per half clock keeps
 * well inside that at any supply. */
#define DELAY_US 1

static uint8_t bcd_to_bin(uint8_t bcd)
{
    return (uint8_t)((bcd >> 4) * 10 + (bcd & 0x0F));
}

static uint8_t bin_to_bcd(uint8_t bin)
{
    return (uint8_t)(((bin / 10) << 4) | (bin % 10));
}

static uint8_t make_cmd(uint8_t reg, bool ram, bool read)
{
    return (uint8_t)(CMD_BASE | (ram ? CMD_RAM : 0) | (reg << 1) | (read ? CMD_READ : 0));
}

static void write_byte(ds1302_t *dev, uint8_t val)
{
    gpio_set_direction(dev->io, GPIO_MODE_OUTPUT);
    for (int i = 0; i < 8; i++) {
        gpio_set_level(dev->io, (val >> i) & 1);
        esp_rom_delay_us(DELAY_US);
        gpio_set_level(dev->sclk, 1);
        esp_rom_delay_us(DELAY_US);
        gpio_set_level(dev->sclk, 0);
    }
}

/* The DS1302 puts the first bit out on the falling edge that ends the
 * command byte, and each following bit on the next falling edge. */
static uint8_t read_byte(ds1302_t *dev)
{
    gpio_set_direction(dev->io, GPIO_MODE_INPUT);
    uint8_t val = 0;
    for (int i = 0; i < 8; i++) {
        esp_rom_delay_us(DELAY_US);
        val |= (uint8_t)(gpio_get_level(dev->io) << i);
        gpio_set_level(dev->sclk, 1);
        esp_rom_delay_us(DELAY_US);
        gpio_set_level(dev->sclk, 0);
    }
    return val;
}

static void begin(ds1302_t *dev)
{
    gpio_set_level(dev->sclk, 0);
    gpio_set_level(dev->ce, 1);
    esp_rom_delay_us(4); /* tCC: CE to first clock */
}

static void end(ds1302_t *dev)
{
    gpio_set_level(dev->ce, 0);
    gpio_set_direction(dev->io, GPIO_MODE_OUTPUT);
    gpio_set_level(dev->io, 0);
    esp_rom_delay_us(4); /* tCWH: CE inactive time */
}

static void write_reg(ds1302_t *dev, uint8_t reg, bool ram, uint8_t data)
{
    begin(dev);
    write_byte(dev, make_cmd(reg, ram, false));
    write_byte(dev, data);
    end(dev);
}

static uint8_t read_reg(ds1302_t *dev, uint8_t reg, bool ram)
{
    begin(dev);
    write_byte(dev, make_cmd(reg, ram, true));
    uint8_t val = read_byte(dev);
    end(dev);
    return val;
}

static void write_protect(ds1302_t *dev, bool enable)
{
    write_reg(dev, REG_WP, false, enable ? WP_ENABLE : 0x00);
}

esp_err_t ds1302_init(ds1302_t *dev, int ce_pin, int sclk_pin, int io_pin)
{
    dev->ce = ce_pin;
    dev->sclk = sclk_pin;
    dev->io = io_pin;

    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << ce_pin) | (1ULL << sclk_pin) | (1ULL << io_pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) return err;

    gpio_set_level(ce_pin, 0);
    gpio_set_level(sclk_pin, 0);
    gpio_set_level(io_pin, 0);

    /* The backup cell is a CR1220, which must never be charged. The charger
     * is off after a cold start, but make sure. */
    write_protect(dev, false);
    write_reg(dev, REG_TRICKLE, false, TRICKLE_OFF);
    write_protect(dev, true);

    return ESP_OK;
}

static uint8_t decode_hours(uint8_t raw)
{
    if (raw & HOURS_12H) {
        /* 12-hour mode, as another firmware may have left it. */
        uint8_t h = bcd_to_bin(raw & 0x1F) % 12;
        return (raw & HOURS_PM) ? (uint8_t)(h + 12) : h;
    }
    return bcd_to_bin(raw & 0x3F);
}

bool ds1302_get_time(ds1302_t *dev, ds1302_time_t *time)
{
    begin(dev);
    write_byte(dev, make_cmd(REG_BURST, false, true));
    uint8_t sec = read_byte(dev);
    uint8_t min = read_byte(dev);
    uint8_t hr = read_byte(dev);
    uint8_t day = read_byte(dev);
    uint8_t mon = read_byte(dev);
    uint8_t wday = read_byte(dev);
    uint8_t yr = read_byte(dev);
    end(dev);

    time->seconds = bcd_to_bin(sec & 0x7F);
    time->minutes = bcd_to_bin(min & 0x7F);
    time->hours = decode_hours(hr);
    time->day = bcd_to_bin(day & 0x3F);
    time->month = bcd_to_bin(mon & 0x1F);
    time->weekday = bcd_to_bin(wday & 0x07);
    time->year = bcd_to_bin(yr);

    /* An unconnected I/O line reads all ones or all zeros, both of which
     * fail these checks. */
    return !(sec & SECONDS_CH) &&
           time->seconds < 60 && time->minutes < 60 && time->hours < 24 &&
           time->day >= 1 && time->day <= 31 &&
           time->month >= 1 && time->month <= 12 &&
           time->weekday >= 1 && time->weekday <= 7 &&
           time->year < 100;
}

int ds1302_get_seconds(ds1302_t *dev)
{
    uint8_t raw = read_reg(dev, REG_SECONDS, false);
    if (raw & SECONDS_CH) return -1;
    uint8_t s = bcd_to_bin(raw & 0x7F);
    return s < 60 ? s : -1;
}

void ds1302_set_time(ds1302_t *dev, const ds1302_time_t *time)
{
    write_protect(dev, false);

    begin(dev);
    write_byte(dev, make_cmd(REG_BURST, false, false));
    write_byte(dev, bin_to_bcd(time->seconds) & 0x7F); /* CH = 0: run */
    write_byte(dev, bin_to_bcd(time->minutes));
    write_byte(dev, bin_to_bcd(time->hours) & 0x3F);   /* 24-hour mode */
    write_byte(dev, bin_to_bcd(time->day));
    write_byte(dev, bin_to_bcd(time->month));
    write_byte(dev, bin_to_bcd(time->weekday));
    write_byte(dev, bin_to_bcd(time->year));
    write_byte(dev, WP_ENABLE); /* burst ends with the WP register */
    end(dev);
}

uint8_t ds1302_read_ram(ds1302_t *dev, uint8_t addr)
{
    if (addr >= DS1302_RAM_SIZE) addr = DS1302_RAM_SIZE - 1;
    return read_reg(dev, addr, true);
}

void ds1302_write_ram(ds1302_t *dev, uint8_t addr, uint8_t data)
{
    if (addr >= DS1302_RAM_SIZE) addr = DS1302_RAM_SIZE - 1;
    write_protect(dev, false);
    write_reg(dev, addr, true, data);
    write_protect(dev, true);
}
