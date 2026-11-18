#include "gyro_bias.h"
#include <math.h>
#include <string.h>

#define ACCEL_MIN_MPS2 (0.85f * 9.80665f)
#define ACCEL_MAX_MPS2 (1.15f * 9.80665f)
#define ACCEL_MAX_DELTA_MPS2 0.35f
#define GYRO_MAX_STDDEV_RADPS 0.006f

bool gyro_bias_push(gyro_bias_window_t *window, const float accel_mps2[3],
                    const float gyro_radps[3]) {
    const float accel_sq = accel_mps2[0] * accel_mps2[0] +
                           accel_mps2[1] * accel_mps2[1] +
                           accel_mps2[2] * accel_mps2[2];
    if (!(accel_sq >= ACCEL_MIN_MPS2 * ACCEL_MIN_MPS2 &&
          accel_sq <= ACCEL_MAX_MPS2 * ACCEL_MAX_MPS2)) {
        return false;
    }
    for (unsigned axis = 0U; axis < 3U; ++axis) {
        if (!(fabsf(gyro_radps[axis]) <= GYRO_BIAS_MAX_RADPS) ||
            (window->samples > 0U &&
             fabsf(accel_mps2[axis] - window->first_accel_mps2[axis]) >
                 ACCEL_MAX_DELTA_MPS2)) {
            return false;
        }
    }
    if (window->samples == 0U) {
        memcpy(window->first_accel_mps2, accel_mps2,
               sizeof(window->first_accel_mps2));
    }
    ++window->samples;
    for (unsigned axis = 0U; axis < 3U; ++axis) {
        const float delta = gyro_radps[axis] - window->mean_radps[axis];
        window->mean_radps[axis] += delta / (float)window->samples;
        window->m2[axis] +=
            delta * (gyro_radps[axis] - window->mean_radps[axis]);
    }
    return true;
}

bool gyro_bias_result(const gyro_bias_window_t *window, float bias_radps[3]) {
    if (window->samples < 2U) {
        return false;
    }
    const float max_m2 = GYRO_MAX_STDDEV_RADPS * GYRO_MAX_STDDEV_RADPS *
                         (float)(window->samples - 1U);
    for (unsigned axis = 0U; axis < 3U; ++axis) {
        if (window->m2[axis] > max_m2) {
            return false;
        }
    }
    memcpy(bias_radps, window->mean_radps, sizeof(window->mean_radps));
    return true;
}
