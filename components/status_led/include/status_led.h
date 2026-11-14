#ifndef STATUS_LED_H
#define STATUS_LED_H

typedef enum {
    STATUS_LED_OFF,
    STATUS_LED_BOOT,            /* white, solid */
    STATUS_LED_PORTAL,          /* blue, slow blink */
    STATUS_LED_WIFI_CONNECTING, /* yellow, fast blink */
    STATUS_LED_WIFI_FAILED,     /* red, slow blink */
    STATUS_LED_RUNNING,         /* green, 1 Hz heartbeat from main loop */
} status_led_state_t;

void status_led_init(int gpio);
void status_led_set(status_led_state_t state);

/*
 * Activity heartbeat: briefly blank the LED (~60 ms) then restore the
 * current colour. Only acts on solid states so it never fights a blink
 * pattern. Call it from the main loop once per tick.
 */
void status_led_pulse(void);

#endif
