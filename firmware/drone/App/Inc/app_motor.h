#ifndef APP_MOTOR_H
#define APP_MOTOR_H

#include "normalized.h"
#include <stdbool.h>

#define APP_MOTOR_COUNT 4U

/**
 * @brief Start all motor PWM outputs at the minimum command (0.0).
 * @pre CubeMX has initialized the PWM timer and output pins.
 * @return true on success; false leaves all outputs disabled.
 * @note Nonblocking foreground only; never call concurrently or from an ISR.
 * Reinitialization resets all outputs. No arming, idle or failsafe is provided.
 */
bool app_motor_init(void);

/**
 * @brief Queue four normalized ESC commands for the same PWM frame.
 * @param command_norm Non-NULL array of four Q15 values in [0,NORM_Q15_ONE],
 * ordered by airframe motor M1..M4, matching mixer_result_t.motor:
 * M1 = PB8 / TIM4 CH3, M2 = PB9 / TIM4 CH4,
 * M3 = PB6 / TIM4 CH1, M4 = PB7 / TIM4 CH2.
 * Values are dimensionless ESC commands, not thrust or RPM fractions:
 * 0 maps to 1000 us, NORM_Q15_ONE to 2000 us. Conversion uses only integers,
 * rounding to the nearest microsecond (half upward).
 * The array is consumed during the call and is not retained.
 * @return false if outputs are stopped, the pointer is NULL, or any command
 * is invalid; no output is changed on failure. Values are not clamped.
 * @note Nonblocking foreground only; requires successful app_motor_init().
 * This writes raw motor commands without checking an armed state.
 */
bool app_motor_set_all(const norm_q15_t command_norm[APP_MOTOR_COUNT]);

/**
 * @brief Disable all motor PWM outputs until initialization succeeds again.
 * @pre CubeMX has initialized the PWM timer. May be called repeatedly.
 * @note Nonblocking foreground only; never call concurrently or from an ISR.
 * Signal loss differs from command 0.0; ESC behavior depends on the ESC.
 */
void app_motor_stop(void);

#endif
