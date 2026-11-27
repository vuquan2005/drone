#pragma once

#include <stdbool.h>

typedef struct {
    float gyro_radps[3];
    float accel_mps2[3];
    float accel_z1[3];
    float accel_z2[3];
    float gyro_tau_s;
    float accel_b0;
    float accel_a1;
    float accel_a2;
    bool initialized;
} imu_lowpass_t;

/** Configure the per-axis gyro PT1 and accel Butterworth.
 * Pass writable state and the nominal sample rate. Both cutoffs must be
 * positive and below half the sample rate.
 */
void imu_lowpass_init(imu_lowpass_t *state, float sample_rate_hz,
                      float gyro_cutoff_hz, float accel_cutoff_hz);

/** Update from a fresh calibrated body-frame sample.
 * Pass the elapsed sample interval in seconds. After clearing initialized,
 * the next sample seeds both filters. Input vectors must contain three axes.
 */
void imu_lowpass_update(imu_lowpass_t *state, const float gyro_radps[3],
                        const float accel_mps2[3], float dt_s);
