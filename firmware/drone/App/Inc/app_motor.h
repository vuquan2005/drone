#ifndef APP_MOTOR_H
#define APP_MOTOR_H

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
 * @param command_norm Non-NULL array of four finite values in [0,1], ordered
 * by PWM channel 1..4 (PB6..PB9), not by airframe motor position.
 * Values are dimensionless ESC commands, not thrust or RPM fractions:
 * 0 maps to 1000 us, 1 to 2000 us, with rounding to the nearest microsecond.
 * The array is consumed during the call and is not retained.
 * @return false if outputs are stopped, the pointer is NULL, or any command
 * is invalid; no output is changed on failure. Values are not clamped.
 * @note Nonblocking foreground only; requires successful app_motor_init().
 * This writes raw motor commands without checking an armed state.
 */
bool app_motor_set_all(const float command_norm[APP_MOTOR_COUNT]);

/**
 * @brief Disable all motor PWM outputs until initialization succeeds again.
 * @pre CubeMX has initialized the PWM timer. May be called repeatedly.
 * @note Nonblocking foreground only; never call concurrently or from an ISR.
 * Signal loss differs from command 0.0; ESC behavior depends on the ESC.
 */
void app_motor_stop(void);

#endif
