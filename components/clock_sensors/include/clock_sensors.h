#ifndef CLOCK_SENSORS_H
#define CLOCK_SENSORS_H

#include "esp_err.h"

/**
 * @file clock_sensors.h
 * @brief NTC thermistor and LDR readings from the HU-058 PCB.
 *
 * Both sensors sit at the bottom of a divider with a 10K pull-up. The LDR's
 * pull-up goes to 3V3; the NTC's can go to a GPIO instead, which powers the
 * divider only while it is read. Readings are averaged over several samples
 * and, where the chip supports it, converted through the ADC calibration
 * scheme.
 */

/**
 * @brief Configure the ADC for both sensor pins (must share one ADC unit).
 *
 * @param ntc_power_gpio GPIO feeding the NTC divider's pull-up, driven high
 *                       only while reading; -1 if the pull-up is on 3V3.
 */
esp_err_t clock_sensors_init(int ntc_gpio, int ldr_gpio, int ntc_power_gpio);

/**
 * @brief Thermistor temperature in Celsius, NAN if the sensor reads open
 *        or shorted. With a switched supply this busy-waits ~8ms while the
 *        divider settles.
 */
float clock_sensors_temperature_c(void);

/**
 * @brief LDR resistance in ohms (higher is darker), NAN if open or shorted.
 */
float clock_sensors_ldr_ohms(void);

#endif
