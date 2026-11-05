#ifndef BSP_PWM_H
#define BSP_PWM_H

#include <stdbool.h>
#include <stdint.h>

#define BSP_PWM_CHANNEL_COUNT 4U
#define BSP_PWM_MIN_PULSE_US 1000U
#define BSP_PWM_MAX_PULSE_US 2000U
#define BSP_PWM_PERIOD_US 2500U

/**
 * @brief Start TIM4 CH1..CH4 (PB6..PB9) at 1000 us, 400 Hz.
 * @pre CubeMX has initialized TIM4 PWM1, active high, PSC=71, ARR=2499,
 * with a 72 MHz timer clock and the four GPIO alternate-function outputs.
 * TIM4 is exclusively owned by this module after CubeMX initialization.
 * @return true on success; false leaves all outputs disabled.
 * @note Nonblocking foreground API, never call concurrently or from an ISR.
 * Reinitialization stops all channels and resets every pulse to the minimum.
 * This is raw ESC PWM output; it does not implement arming or failsafe.
 */
bool bsp_pwm_init(void);

/**
 * @brief Queue one channel's pulse width for the next timer update.
 * @param channel Output number 1..4, mapping to TIM4 CH1..CH4 / PB6..PB9.
 * @param pulse_us Pulse width in [1000, 2000] microseconds.
 * @return false if stopped, channel is invalid, or pulse is out of range;
 * no compare register is changed on failure.
 * @note Nonblocking foreground only; requires successful bsp_pwm_init().
 */
bool bsp_pwm_set_pulse_us(uint8_t channel, uint16_t pulse_us);

/**
 * @brief Queue four pulse widths together for a timer update.
 * @param pulse_us Non-NULL array of four widths in [1000, 2000] us, CH1 first.
 * The array is consumed during the call and is not retained.
 * @return false if stopped, NULL, or any width is out of range; no compare
 * register is changed on failure.
 * @note Nonblocking foreground only. Update events are briefly suppressed so
 * all widths take effect in the same frame; a coincident update may be skipped.
 */
bool bsp_pwm_set_all_us(const uint16_t pulse_us[BSP_PWM_CHANNEL_COUNT]);

/**
 * @brief Disable all four PWM outputs; further writes fail until init succeeds.
 * @pre CubeMX has initialized TIM4. May be called repeatedly.
 * @note Nonblocking foreground only. Disabling pulses is distinct from sending
 * a minimum-throttle command; ESC signal-loss behavior depends on the ESC.
 */
void bsp_pwm_stop(void);

#endif
