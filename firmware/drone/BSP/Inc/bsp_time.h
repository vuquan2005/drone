/**
 * @file bsp_time.h
 * @brief Board delays and MCU read timestamps.
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

/** @brief Read MCU uptime in microseconds, wrapping modulo 2^32.
 * @pre HAL uses its default 1 ms SysTick time base, clocked from HCLK;
 * SystemCoreClock is stable, up to date and an integer multiple of 1 MHz.
 * SysTick interrupts must be enabled and serviced promptly. Foreground only;
 * never call with interrupts masked or from an ISR.
 * @note Combines HAL tick and the current SysTick countdown, retrying across
 * tick boundaries. Does not change registers, mask interrupts or use DWT.
 * This is a read timestamp, not the sensor's acquisition time or wall time.
 * Debug halt/sleep/tick suspension can affect the time base.
 */
uint32_t bsp_time_now_us(void);

#endif
