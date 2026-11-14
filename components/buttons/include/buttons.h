#ifndef BUTTONS_H
#define BUTTONS_H

#include <stdbool.h>

typedef enum {
    BUTTON_1_PRESS,
    BUTTON_1_RELEASE,
    BUTTON_2_PRESS,
    BUTTON_2_RELEASE,
    BUTTON_1_HELD,
    BUTTON_BOTH_HELD,
} button_event_t;

typedef void (*button_callback_t)(button_event_t event);

/**
 * @brief Initialise the button debounce state machine.
 * @param callback  Function called on each debounced button event.
 */
void buttons_init(button_callback_t callback);

/**
 * @brief Feed raw button samples into the debounce engine.
 *
 * Call this at a fixed 20ms interval from the HAL timer.
 * @param btn1_pressed  true if button 1 is currently pressed.
 * @param btn2_pressed  true if button 2 is currently pressed.
 */
void buttons_poll(bool btn1_pressed, bool btn2_pressed);

#endif
