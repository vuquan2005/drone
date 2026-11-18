#ifndef GYRO_BIAS_H
#define GYRO_BIAS_H

#include <stdbool.h>
#include <stdint.h>

#define GYRO_BIAS_MAX_RADPS 0.08726646f /* 5 deg/s */

/** Running statistics for a stationary gyro calibration in body FRD. */
typedef struct {
    uint32_t samples;
    float first_accel_mps2[3];
    float mean_radps[3];
    float m2[3];
} gyro_bias_window_t;

/** Add one paired FRD sample to a zero-initialized or active window. Returns
 * false without changing the window if the sample fails the stationary gate.
 * Pass readable 3-element accel (m/s^2) and gyro (rad/s) arrays and a valid
 * window. The caller handles sample freshness and restarts on movement.
 */
bool gyro_bias_push(gyro_bias_window_t *window, const float accel_mps2[3],
                    const float gyro_radps[3]);

/** Copy the mean (rad/s, FRD) if all gyro axes pass the variance gate.
 * The caller checks minimum duration and count. Pass a valid window and a
 * writable, non-aliasing 3-element output. False leaves the output untouched.
 */
bool gyro_bias_result(const gyro_bias_window_t *window, float bias_radps[3]);

#endif
