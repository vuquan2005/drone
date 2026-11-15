#include "mixer.h"
#include <math.h>
#include <stddef.h>

static const int32_t roll_sign[MIXER_MOTOR_COUNT] = {-1, -1, 1, 1};
static const int32_t pitch_sign[MIXER_MOTOR_COUNT] = {1, -1, -1, 1};
static const int32_t yaw_sign[MIXER_MOTOR_COUNT] = {1, -1, 1, -1};

static int32_t clamp(int32_t value, int32_t low, int32_t high) {
    return value < low ? low : (value > high ? high : value);
}

static norm_q15_t quantize(float value) {
    const float scaled = value * NORM_Q15_ONE;
    const norm_q15_t integral = (norm_q15_t)scaled;
    const float fraction = scaled - integral;
    return integral + (fraction >= 0.5f) - (fraction <= -0.5f);
}

static norm_q15_t scale(norm_q15_t value, norm_q15_t fraction) {
    return (norm_q15_t)((int64_t)value * fraction / NORM_Q15_ONE);
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

    const mixer_fixed_command_t requested = {
        quantize(command->throttle), quantize(command->roll),
        quantize(command->pitch), quantize(command->yaw)};
    norm_q15_t rp[MIXER_MOTOR_COUNT];
    norm_q15_t low = 0;
    norm_q15_t high = 0;
    for (unsigned i = 0; i < MIXER_MOTOR_COUNT; ++i) {
        rp[i] =
            (requested.roll * roll_sign[i] + requested.pitch * pitch_sign[i]) /
            2;
        if (rp[i] < low)
            low = rp[i];
        if (rp[i] > high)
            high = rp[i];
    }
    const norm_q15_t span = high - low;
    const norm_q15_t rp_scale =
        span > NORM_Q15_ONE
            ? (norm_q15_t)((int64_t)NORM_Q15_ONE * NORM_Q15_ONE / span)
            : NORM_Q15_ONE;
    for (unsigned i = 0; i < MIXER_MOTOR_COUNT; ++i) {
        rp[i] = scale(rp[i], rp_scale);
    }

    /* Every ordered pair must obey output[i]-output[j] <= ONE.
     * Round the retained fraction down so integer allocation stays feasible.
     * Opposite yaw contributions are symmetric after truncation. */
    const norm_q15_t yaw_half = requested.yaw / 2;
    norm_q15_t yaw_scale = NORM_Q15_ONE;
    for (unsigned i = 0; i < MIXER_MOTOR_COUNT; ++i) {
        for (unsigned j = 0; j < MIXER_MOTOR_COUNT; ++j) {
            const norm_q15_t slope = yaw_half * (yaw_sign[i] - yaw_sign[j]);
            if (slope > 0) {
                const int64_t bound =
                    (int64_t)(NORM_Q15_ONE - (rp[i] - rp[j])) * NORM_Q15_ONE /
                    slope;
                if (bound < yaw_scale) {
                    yaw_scale = (norm_q15_t)bound;
                }
            }
        }
    }
    const norm_q15_t yaw = scale(yaw_half, yaw_scale);
    low = 0;
    high = 0;
    for (unsigned i = 0; i < MIXER_MOTOR_COUNT; ++i) {
        rp[i] += yaw * yaw_sign[i];
        if (rp[i] < low)
            low = rp[i];
        if (rp[i] > high)
            high = rp[i];
    }
    const norm_q15_t collective =
        clamp(requested.throttle, -low, NORM_Q15_ONE - high);
    for (unsigned i = 0; i < MIXER_MOTOR_COUNT; ++i) {
        const norm_q15_t output = collective + rp[i];
        result->motor[i] = output;
        result->achieved.throttle += output;
        result->achieved.roll += roll_sign[i] * output;
        result->achieved.pitch += pitch_sign[i] * output;
        result->achieved.yaw += yaw_sign[i] * output;
    }
    result->achieved.throttle /= 4;
    result->achieved.roll /= 2;
    result->achieved.pitch /= 2;
    result->achieved.yaw /= 2;
    result->residual = (mixer_fixed_command_t){
        requested.throttle - result->achieved.throttle,
        requested.roll - result->achieved.roll,
        requested.pitch - result->achieved.pitch,
        requested.yaw - result->achieved.yaw,
    };
    result->roll_pitch_scale = rp_scale;
    result->yaw_scale = yaw_scale;
    return true;
}
