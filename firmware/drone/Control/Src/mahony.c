#include "mahony.h"

#include <math.h>
#include <stddef.h>

bool mahony_init(mahony_t *state, const float accel_mps2[3]) {
    const float horizontal = hypotf(accel_mps2[1], accel_mps2[2]);
    const float magnitude = hypotf(accel_mps2[0], horizontal);
    if (!isfinite(magnitude) || magnitude <= 0.0f) {
        return false;
    }

    const float roll =
        horizontal > 0.0f ? atan2f(-accel_mps2[1], -accel_mps2[2]) : 0.0f;
    const float half_pitch = 0.5f * atan2f(accel_mps2[0], horizontal);
    const float cr = cosf(0.5f * roll);
    const float sr = sinf(0.5f * roll);
    const float cp = cosf(half_pitch);
    const float sp = sinf(half_pitch);

    *state = (mahony_t){.q = {cr * cp, sr * cp, cr * sp, -sr * sp}};
    return true;
}

bool mahony_update(mahony_t *state, const float gyro_radps[3],
                   const float accel_mps2[3], float dt_s, float kp, float ki) {
    if (!isfinite(dt_s) || dt_s <= 0.0f || !isfinite(kp) || kp < 0.0f ||
        !isfinite(ki) || ki < 0.0f) {
        return false;
    }

    mahony_t next = *state;
    const float w = state->q[0];
    const float x = state->q[1];
    const float y = state->q[2];
    const float z = state->q[3];
    float error[3] = {0};
    if (accel_mps2 != NULL) {
        const float ax = accel_mps2[0];
        const float ay = accel_mps2[1];
        const float az = accel_mps2[2];
        const float magnitude = sqrtf(ax * ax + ay * ay + az * az);
        if (!isfinite(magnitude) || magnitude <= 0.0f) {
            return false;
        }

        /* Predicted down vector in body axes. Accelerometer measures up at
         * rest. */
        const float dx = 2.0f * (x * z - w * y);
        const float dy = 2.0f * (w * x + y * z);
        const float dz = 1.0f - 2.0f * (x * x + y * y);
        error[0] = (az * dy - ay * dz) / magnitude;
        error[1] = (ax * dz - az * dx) / magnitude;
        error[2] = (ay * dx - ax * dy) / magnitude;
    }

    float step[3];
    for (size_t axis = 0; axis < 3; ++axis) {
        next.integral_radps[axis] =
            ki > 0.0f ? next.integral_radps[axis] + ki * error[axis] * dt_s
                      : 0.0f;
        step[axis] =
            0.5f * dt_s *
            (gyro_radps[axis] + kp * error[axis] + next.integral_radps[axis]);
    }

    next.q[0] += -x * step[0] - y * step[1] - z * step[2];
    next.q[1] += w * step[0] + y * step[2] - z * step[1];
    next.q[2] += w * step[1] - x * step[2] + z * step[0];
    next.q[3] += w * step[2] + x * step[1] - y * step[0];
    const float norm = sqrtf(next.q[0] * next.q[0] + next.q[1] * next.q[1] +
                             next.q[2] * next.q[2] + next.q[3] * next.q[3]);
    if (!isfinite(norm) || norm <= 0.0f) {
        return false;
    }
    for (size_t i = 0; i < 4; ++i) {
        next.q[i] /= norm;
    }
    *state = next;
    return true;
}
