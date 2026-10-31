#ifndef BSP_TIME_H
#define BSP_TIME_H

#include <stdint.h>

/* Call after SystemClock_Config, before using bsp_time_delay_us.
 * Uses the Cortex-M3 DWT counter without resetting a running counter. */
void bsp_time_init(void);
void bsp_time_delay_us(uint32_t period_us);

#endif
