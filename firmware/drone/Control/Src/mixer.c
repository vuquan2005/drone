#include "mixer.h"
#include <math.h>
#include <stddef.h>

static const float roll_factor[MIXER_MOTOR_COUNT] = {-0.5f, -0.5f, 0.5f, 0.5f};
static const float pitch_factor[MIXER_MOTOR_COUNT] = {0.5f, -0.5f, -0.5f, 0.5f};
static const float yaw_factor[MIXER_MOTOR_COUNT] = {0.5f, -0.5f, 0.5f, -0.5f};

static float clamp(float value, float low, float high) {
    return fminf(fmaxf(value, low), high);
}

bool mixer_quad_x(const mixer_command_t *command, mixer_result_t *result) {
    if (result == NULL) {
        return false;
    }
    *result = (mixer_result_t){0};
    if (command == NULL || !isfinite(command->throttle) ||
        !(command->throttle >= 0.0f && command->throttle <= 1.0f) ||
        !isfinite(command->roll) || fabsf(command->roll) > 1.0f ||
        !isfinite(command->pitch) || fabsf(command->pitch) > 1.0f ||
        !isfinite(command->yaw) || fabsf(command->yaw) > 1.0f) {
        return false;
    }

    float rp[MIXER_MOTOR_COUNT];
    float low = 0.0f;
    float high = 0.0f;
    for (unsigned i = 0; i < MIXER_MOTOR_COUNT; ++i) {
        rp[i] =
            command->roll * roll_factor[i] + command->pitch * pitch_factor[i];
        low = fminf(low, rp[i]);
        high = fmaxf(high, rp[i]);
    }
    const float span = high - low;
    const float rp_scale = span > 1.0f ? 1.0f / span : 1.0f;
    for (unsigned i = 0; i < MIXER_MOTOR_COUNT; ++i) {
        rp[i] *= rp_scale;
    }

    /* Every ordered pair must obey output[i]-output[j] <= 1.
     * The RP-only point is feasible, so positive yaw slopes give upper bounds
     * on the retained fraction; negative slopes cannot tighten that bound. */
    float yaw_scale = 1.0f;
    for (unsigned i = 0; i < MIXER_MOTOR_COUNT; ++i) {
        for (unsigned j = 0; j < MIXER_MOTOR_COUNT; ++j) {
            const float slope = command->yaw * (yaw_factor[i] - yaw_factor[j]);
            if (slope > 0.0f) {
                const float bound = (1.0f - (rp[i] - rp[j])) / slope;
                yaw_scale = fminf(yaw_scale, clamp(bound, 0.0f, 1.0f));
            }
        }
    }
    low = 0.0f;
    high = 0.0f;
    for (unsigned i = 0; i < MIXER_MOTOR_COUNT; ++i) {
        rp[i] += yaw_scale * command->yaw * yaw_factor[i];
        low = fminf(low, rp[i]);
        high = fmaxf(high, rp[i]);
    }
    const float collective = clamp(command->throttle, -low, 1.0f - high);
    for (unsigned i = 0; i < MIXER_MOTOR_COUNT; ++i) {
        /* Clamp only removes floating-point roundoff after allocation. */
        const float output = clamp(collective + rp[i], 0.0f, 1.0f);
        result->motor[i] = output;
        result->achieved.throttle += 0.25f * output;
        result->achieved.roll += roll_factor[i] * output;
        result->achieved.pitch += pitch_factor[i] * output;
        result->achieved.yaw += yaw_factor[i] * output;
    }
    result->residual = (mixer_command_t){
        command->throttle - result->achieved.throttle,
        command->roll - result->achieved.roll,
        command->pitch - result->achieved.pitch,
        command->yaw - result->achieved.yaw,
    };
    result->roll_pitch_scale = rp_scale;
    result->yaw_scale = yaw_scale;
    return true;
}
