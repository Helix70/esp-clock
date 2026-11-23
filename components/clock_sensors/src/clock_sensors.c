#include "clock_sensors.h"

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"

#include <math.h>

static const char *TAG = "sensors";

#define SUPPLY_MV 3300.0f
#define R_PULLUP 10000.0f
#define SAMPLES 16

/*
 * The 100nF across the NTC charges through 10K || NTC: about 0.5ms at 25C,
 * 0.85ms at -10C. Settling a full-scale step to half an LSB at 12 bits takes
 * 9 tau, so 8ms covers any room temperature. Busy-waited rather than
 * vTaskDelay'd, since at 100Hz a tick-based wait would keep the divider
 * powered up to twice as long. Each reading self-heats the NTC for ~9ms.
 */
#define NTC_SETTLE_US 8000

#define NTC_BETA 3950.0f
#define NTC_R0 10000.0f
#define NTC_T0 298.15f

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static adc_channel_t s_ntc_channel;
static adc_channel_t s_ldr_channel;
static int s_ntc_power_gpio = -1;
static clock_sensors_adc_t s_ntc_last = {-1, -1};
static clock_sensors_adc_t s_ldr_last = {-1, -1};

static void cali_init(adc_unit_t unit)
{
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cfg = {
        .unit_id = unit,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&cfg, &s_cali) == ESP_OK) {
        ESP_LOGI(TAG, "ADC calibration: curve fitting");
        return;
    }
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cfg = {
        .unit_id = unit,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_line_fitting(&cfg, &s_cali) == ESP_OK) {
        ESP_LOGI(TAG, "ADC calibration: line fitting");
        return;
    }
#else
    (void)unit;
#endif
    s_cali = NULL;
    ESP_LOGW(TAG, "No ADC calibration available, using raw scaling");
}

esp_err_t clock_sensors_init(int ntc_gpio, int ldr_gpio, int ntc_power_gpio)
{
    /* Divider unpowered between readings: no self-heating, no idle current. */
    if (ntc_power_gpio >= 0) {
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << ntc_power_gpio,
            .mode = GPIO_MODE_OUTPUT,
        };
        ESP_ERROR_CHECK(gpio_config(&io));
        ESP_ERROR_CHECK(gpio_set_level(ntc_power_gpio, 0));
        s_ntc_power_gpio = ntc_power_gpio;
    }

    adc_unit_t ntc_unit, ldr_unit;
    ESP_ERROR_CHECK(adc_oneshot_io_to_channel(ntc_gpio, &ntc_unit, &s_ntc_channel));
    ESP_ERROR_CHECK(adc_oneshot_io_to_channel(ldr_gpio, &ldr_unit, &s_ldr_channel));
    if (ntc_unit != ldr_unit) {
        ESP_LOGE(TAG, "NTC and LDR must be on the same ADC unit");
        return ESP_ERR_INVALID_ARG;
    }

    adc_oneshot_unit_init_cfg_t unit_cfg = {.unit_id = ntc_unit};
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &s_adc));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, s_ntc_channel, &chan_cfg));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, s_ldr_channel, &chan_cfg));

    cali_init(ntc_unit);
    return ESP_OK;
}

/* Averaged divider ratio (sensor voltage / supply), NAN on read failure.
 * Records the averaged raw code and millivolts in *last. */
static float read_ratio(adc_channel_t ch, clock_sensors_adc_t *last)
{
    int raw_sum = 0, mv_sum = 0, n = 0;
    for (int i = 0; i < SAMPLES; i++) {
        int raw;
        if (adc_oneshot_read(s_adc, ch, &raw) != ESP_OK) continue;
        if (s_cali) {
            int mv;
            if (adc_cali_raw_to_voltage(s_cali, raw, &mv) != ESP_OK) continue;
            mv_sum += mv;
        }
        raw_sum += raw;
        n++;
    }
    if (n == 0) {
        last->raw = -1;
        last->mv = -1;
        return NAN;
    }

    last->raw = (raw_sum + n / 2) / n;
    last->mv = s_cali ? (mv_sum + n / 2) / n : -1;
    return s_cali ? (float)mv_sum / n / SUPPLY_MV : (float)raw_sum / n / 4095.0f;
}

/* Bottom resistor of the divider, NAN if the reading is pinned at a rail. */
static float divider_ohms(adc_channel_t ch, clock_sensors_adc_t *last)
{
    float r = read_ratio(ch, last);
    if (isnan(r) || r < 0.01f || r > 0.99f) return NAN;
    return R_PULLUP * r / (1.0f - r);
}

float clock_sensors_temperature_c(void)
{
    float r_ntc;
    if (s_ntc_power_gpio >= 0) {
        gpio_set_level(s_ntc_power_gpio, 1);
        esp_rom_delay_us(NTC_SETTLE_US);
        r_ntc = divider_ohms(s_ntc_channel, &s_ntc_last);
        gpio_set_level(s_ntc_power_gpio, 0);
    } else {
        r_ntc = divider_ohms(s_ntc_channel, &s_ntc_last);
    }
    if (isnan(r_ntc)) return NAN;

    float t_kelvin = 1.0f / (1.0f / NTC_T0 + logf(r_ntc / NTC_R0) / NTC_BETA);
    return t_kelvin - 273.15f;
}

float clock_sensors_ldr_ohms(void)
{
    return divider_ohms(s_ldr_channel, &s_ldr_last);
}

void clock_sensors_last_adc(clock_sensors_adc_t *ntc, clock_sensors_adc_t *ldr)
{
    if (ntc) *ntc = s_ntc_last;
    if (ldr) *ldr = s_ldr_last;
}
