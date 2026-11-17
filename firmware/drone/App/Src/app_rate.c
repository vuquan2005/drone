#include "app_rate.h"
#include <math.h>
#include <stddef.h>

void app_rate_reset(app_rate_t *state) {
    if (state == NULL)
        return;
    rate_controller_reset(&state->controller);
    state->mixer = (mixer_result_t){0};
    for (unsigned i = 0; i < RATE_AXIS_COUNT; ++i)
        state->residual[i] = 0.0f;
    state->previous_us = 0;
    state->have_time = false;
    state->valid = false;
}

bool app_rate_configure(app_rate_t *state, const app_rate_config_t *config) {
    if (state == NULL)
        return false;
    *state = (app_rate_t){0};
    if (config == NULL)
        return false;
    rate_controller_t probe = {0};
    float zero[RATE_AXIS_COUNT] = {0}, output[RATE_AXIS_COUNT];
    if (!rate_controller_step(&probe, config->axis, zero, zero, 0.001f, zero,
                              output))
        return false;
    for (unsigned i = 0; i < RATE_AXIS_COUNT; ++i) {
        if (!isfinite(config->max_rate_radps[i]) ||
            config->max_rate_radps[i] <= 0.0f)
            return false;
    }
    state->config = *config;
    state->configured = true;
    return true;
}

bool app_rate_step(app_rate_t *state, const app_command_t *command,
                   const float gyro_flu_radps[RATE_AXIS_COUNT],
                   uint32_t time_us) {
    if (state == NULL)
        return false;
    state->valid = false;
    if (!state->configured || command == NULL || !command->valid ||
        gyro_flu_radps == NULL || command->throttle < 0 ||
        command->throttle > NORM_Q15_ONE)
        goto fail;
    const norm_q15_t stick[RATE_AXIS_COUNT] = {command->roll, command->pitch,
                                               command->yaw};
    float target[RATE_AXIS_COUNT], gyro[RATE_AXIS_COUNT],
        output[RATE_AXIS_COUNT];
    for (unsigned i = 0; i < RATE_AXIS_COUNT; ++i) {
        if (stick[i] < -NORM_Q15_ONE || stick[i] > NORM_Q15_ONE ||
            !isfinite(gyro_flu_radps[i]))
            goto fail;
        target[i] =
            ((float)stick[i] / NORM_Q15_ONE) * state->config.max_rate_radps[i];
        gyro[i] = gyro_flu_radps[i] * (i == 0 ? 1.0f : -1.0f);
    }
    if (!state->have_time) {
        state->previous_us = time_us;
        state->have_time = true;
        return false;
    }
    const uint32_t elapsed_us = time_us - state->previous_us;
    if (elapsed_us == 0 || elapsed_us > 20000U)
        goto fail;
    state->previous_us = time_us;
    if (!rate_controller_step(&state->controller, state->config.axis, target,
                              gyro, elapsed_us * 1e-6f, state->residual,
                              output))
        goto fail;
    const mixer_command_t request = {.throttle = (float)command->throttle /
                                                 NORM_Q15_ONE,
                                     .roll = output[0],
                                     .pitch = output[1],
                                     .yaw = output[2]};
    if (!mixer_quad_x(&request, &state->mixer))
        goto fail;
    state->residual[0] = (float)state->mixer.residual.roll / NORM_Q15_ONE;
    state->residual[1] = (float)state->mixer.residual.pitch / NORM_Q15_ONE;
    state->residual[2] = (float)state->mixer.residual.yaw / NORM_Q15_ONE;
    state->valid = true;
    return true;
fail:
    app_rate_reset(state);
    return false;
}
