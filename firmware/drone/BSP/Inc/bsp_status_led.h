#ifndef BSP_STATUS_LED_H
#define BSP_STATUS_LED_H

#include <stdbool.h>

/** Set the status LED after GPIO initialization. Foreground, nonblocking.
 * @param on True to light the LED, false to turn it off.
 */
void bsp_status_led_write(bool on);

#endif
