#ifndef BUTTONS_HAL_H
#define BUTTONS_HAL_H

#include "buttons.h"

/**
 * @brief Start button polling on ESP32 GPIO pins.
 *
 * Configures two GPIO pins as inputs with internal pull-ups and starts
 * a 20ms periodic timer that feeds samples to the portable debounce engine.
 *
 * @param pin_btn1  GPIO number for button 1 (active low).
 * @param pin_btn2  GPIO number for button 2 (active low).
 * @param callback  Function called on each debounced button event.
 * @return 0 on success, negative on error.
 */
int buttons_hal_init(int pin_btn1, int pin_btn2, button_callback_t callback);

/**
 * @brief Stop the button poll timer.
 */
void buttons_hal_stop(void);

#endif
