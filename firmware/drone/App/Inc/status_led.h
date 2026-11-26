#ifndef STATUS_LED_H
#define STATUS_LED_H

#include <stdint.h>

typedef enum {
    STATUS_LED_WAITING,
    STATUS_LED_DISARMED,
    STATUS_LED_ARMED,
    STATUS_LED_MAINTENANCE,
    STATUS_LED_RC_LOSS,
    STATUS_LED_IMU_FAULT,
    STATUS_LED_MOTOR_FAULT,
    STATUS_LED_OTHER_FAULT,
} status_led_pattern_t;

/** Start a boot pulse after GPIO initialization.
 * @param now_ms Monotonic uptime in milliseconds, wrapping modulo 2^32.
 * Foreground, nonblocking.
 */
void status_led_startup(uint32_t now_ms);

/** Display the requested pattern, with faults interrupting the boot pulse.
 * @param pattern One of the defined status LED patterns.
 * @param now_ms Monotonic uptime in milliseconds, wrapping modulo 2^32.
 * Call regularly from the same foreground context as startup. Nonblocking.
 */
void status_led_update(status_led_pattern_t pattern, uint32_t now_ms);

#endif
