#include "imu_lowpass.h"

#include <math.h>

void imu_lowpass_init(imu_lowpass_t *state, float sample_rate_hz,
                      float gyro_cutoff_hz, float accel_cutoff_hz) {
    /* Solve for the PT1 pole at -3 dB at the nominal sample interval. */
    const float sine = sinf(3.14159265359f * gyro_cutoff_hz / sample_rate_hz);
    const float gyro_tau_samples =
        (sqrtf(1.0f + 1.0f / (sine * sine)) - 1.0f) * 0.5f;
    const float k = tanf(3.14159265359f * accel_cutoff_hz / sample_rate_hz);
    const float norm = 1.0f / (1.0f + 1.41421356237f * k + k * k);
    *state =
        (imu_lowpass_t){.gyro_tau_s = gyro_tau_samples / sample_rate_hz,
                        .accel_b0 = k * k * norm,
                        .accel_a1 = 2.0f * (k * k - 1.0f) * norm,
                        .accel_a2 = (1.0f - 1.41421356237f * k + k * k) * norm};
}

void imu_lowpass_update(imu_lowpass_t *state, const float gyro_radps[3],
                        const float accel_mps2[3], float dt_s) {
    const float alpha = dt_s / (state->gyro_tau_s + dt_s);
    const float b0 = state->accel_b0;

    for (unsigned axis = 0; axis < 3U; ++axis) {
        const float accel = accel_mps2[axis];
        if (!state->initialized) {
            state->gyro_radps[axis] = gyro_radps[axis];
            state->accel_mps2[axis] = accel;
            /* Seed the transposed biquad at its constant-input equilibrium. */
            state->accel_z1[axis] = (1.0f - b0) * accel;
            state->accel_z2[axis] = (b0 - state->accel_a2) * accel;
        } else {
            state->gyro_radps[axis] +=
                alpha * (gyro_radps[axis] - state->gyro_radps[axis]);
            const float output = b0 * accel + state->accel_z1[axis];
            state->accel_z1[axis] = 2.0f * b0 * accel -
                                    state->accel_a1 * output +
                                    state->accel_z2[axis];
            state->accel_z2[axis] = b0 * accel - state->accel_a2 * output;
            state->accel_mps2[axis] = output;
        }
    }
    state->initialized = true;
}
