#include "rate_controller.h"
#include <math.h>
#include <stddef.h>

static float limit(float value, float bound) {
    return value < -bound ? -bound : (value > bound ? bound : value);
}

void rate_controller_reset(rate_controller_t *state) {
    if (state != NULL) {
        *state = (rate_controller_t){0};
    }
}

bool rate_controller_step(rate_controller_t *state,
                          const rate_axis_config_t config[RATE_AXIS_COUNT],
                          const float setpoint_radps[RATE_AXIS_COUNT],
                          const float gyro_radps[RATE_AXIS_COUNT], float dt_s,
                          const float allocation_residual[RATE_AXIS_COUNT],
                          float output[RATE_AXIS_COUNT]) {
    if (output != NULL) {
        for (unsigned i = 0; i < RATE_AXIS_COUNT; ++i) {
            output[i] = 0.0f;
        }
    }
    if (state == NULL || config == NULL || setpoint_radps == NULL ||
        gyro_radps == NULL || allocation_residual == NULL || output == NULL ||
        !isfinite(dt_s) || dt_s <= 0.0f || dt_s > 0.02f) {
        rate_controller_reset(state);
        return false;
    }
    rate_controller_t next = *state;
    float commands[RATE_AXIS_COUNT];
    for (unsigned i = 0; i < RATE_AXIS_COUNT; ++i) {
        const rate_axis_config_t *c = &config[i];
        if (!isfinite(c->kp) || c->kp < 0.0f || !isfinite(c->ki) ||
            c->ki < 0.0f || !isfinite(c->kd) || c->kd < 0.0f ||
            !isfinite(c->integral_limit) || c->integral_limit < 0.0f ||
            c->integral_limit > 1.0f || !isfinite(c->output_limit) ||
            c->output_limit <= 0.0f || c->output_limit > 1.0f ||
            !isfinite(c->d_filter_tau_s) || c->d_filter_tau_s < 0.0f ||
            !isfinite(setpoint_radps[i]) || !isfinite(gyro_radps[i]) ||
            !isfinite(allocation_residual[i])) {
            goto fail;
        }
        const float error = setpoint_radps[i] - gyro_radps[i];
        const float raw_d =
            state->initialized
                ? (gyro_radps[i] - state->previous_gyro[i]) / dt_s
                : 0.0f;
        const float alpha = dt_s / (c->d_filter_tau_s + dt_s);
        const float derivative =
            state->derivative[i] + alpha * (raw_d - state->derivative[i]);
        const float increment = c->ki * error * dt_s;
        const float old_i = limit(state->integral[i], c->integral_limit);
        float integral = limit(old_i + increment, c->integral_limit);
        const float pd = c->kp * error - c->kd * derivative;
        const float candidate = pd + integral;
        /* Reject only the increment that pushes further into saturation. */
        if ((allocation_residual[i] > 0.0f && increment > 0.0f) ||
            (allocation_residual[i] < 0.0f && increment < 0.0f) ||
            (candidate > c->output_limit && increment > 0.0f) ||
            (candidate < -c->output_limit && increment < 0.0f)) {
            integral = old_i;
        }
        if (!isfinite(error) || !isfinite(raw_d) || !isfinite(derivative) ||
            !isfinite(increment) || !isfinite(candidate) || !isfinite(pd)) {
            goto fail;
        }
        next.integral[i] = integral;
        next.previous_gyro[i] = gyro_radps[i];
        next.derivative[i] = derivative;
        commands[i] = limit(pd + integral, c->output_limit);
    }
    next.initialized = true;
    *state = next;
    for (unsigned i = 0; i < RATE_AXIS_COUNT; ++i) {
        output[i] = commands[i];
    }
    return true;
fail:
    rate_controller_reset(state);
    return false;
}
