/**
 * @file bsp_time.h
 * @brief DWT-based blocking microsecond delays for the board.
 */
#ifndef BSP_TIME_H
#define BSP_TIME_H

#include <stdint.h>

/**
 * @brief Enable the Cortex-M3 DWT cycle counter for microsecond delays.
 * @pre SystemClock_Config has completed and SystemCoreClock is up to date.
 * @note Does not reset a running counter. Keep it enabled while using delays.
 */
void bsp_time_init(void);

/**
 * @brief Busy-wait for at least the requested number of microseconds.
 * @param[in] period_us Delay in microseconds; zero returns immediately.
 * @pre bsp_time_init has completed; the core clock remains unchanged and
 * SystemCoreClock reflects its frequency throughout the delay.
 * @note Blocks the foreground; interrupts can extend the delay. Do not call
 * from an ISR. This API provides delays, not sample timestamps.
 */
void bsp_time_delay_us(uint32_t period_us);

#endif
