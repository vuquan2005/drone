#include "attitude_controller.h"
#include <math.h>

bool attitude_controller_step(const float body_to_ned[4], float roll_rad,
                              float pitch_rad, float kp_per_s,
                              float tilt_rate_radps[2]) {
    const float half_pi = 1.5707963268f;
    if (!(fabsf(roll_rad) <= half_pi && fabsf(pitch_rad) <= half_pi) ||
        !isfinite(kp_per_s) || kp_per_s < 0.0f) {
        return false;
    }
    const float w = body_to_ned[0], x = body_to_ned[1], y = body_to_ned[2],
                z = body_to_ned[3];
    const float down_x = 2.0f * (x * z - w * y);
    const float down_y = 2.0f * (y * z + w * x);
    const float down_z = 1.0f - 2.0f * (x * x + y * y);
    const float cos_pitch = cosf(pitch_rad);
    const float target_x = -sinf(pitch_rad);
    const float target_y = sinf(roll_rad) * cos_pitch;
    const float target_z = cosf(roll_rad) * cos_pitch;
    const float dot = target_x * down_x + target_y * down_y + target_z * down_z;
    if (!isfinite(dot) || dot <= -0.999f) {
        return false;
    }

    /* Cross(target down, estimated down) divided by cos(error/2) gives
     * twice the vector part of the shortest tilt-error quaternion. */
    const float gain = kp_per_s / sqrtf(0.5f * (1.0f + dot));
    const float roll_rate = gain * (target_y * down_z - target_z * down_y);
    const float pitch_rate = gain * (target_z * down_x - target_x * down_z);
    if (!isfinite(roll_rate) || !isfinite(pitch_rate)) {
        return false;
    }
    tilt_rate_radps[0] = roll_rate;
    tilt_rate_radps[1] = pitch_rate;
    return true;
}
