#ifndef MAHONY_H
#define MAHONY_H

#include <stdbool.h>

/** @brief Mahony IMU state. Quaternion (w,x,y,z) rotates body FRD to NED
 * with an arbitrary initial heading. Zero-initialize before first use.
 */
typedef struct {
    float q[4];
    float integral_radps[3];
} mahony_t;

/** @brief Initialize roll and pitch from a stationary specific-force sample.
 * @param state Writable filter state.
 * @param accel_mps2 Finite, nonzero body FRD sample (upright Z is negative).
 * @return True on success. On failure the previous state is preserved.
 */
bool mahony_init(mahony_t *state, const float accel_mps2[3]);

/** @brief Update an initialized filter and normalize its quaternion.
 * @param state Writable filter state initialized by mahony_init.
 * @param gyro_radps Angular velocity in body FRD.
 * @param accel_mps2 Body FRD specific force, or NULL for gyro propagation only.
 * @param dt_s Positive elapsed time in seconds.
 * @param kp Nonnegative proportional gain in 1/s.
 * @param ki Nonnegative integral gain in 1/s^2. Zero clears integral feedback.
 * @return True on success. On failure the previous state is preserved.
 */
bool mahony_update(mahony_t *state, const float gyro_radps[3],
                   const float accel_mps2[3], float dt_s, float kp, float ki);

#endif
