#include "app_rate.h"
#include <assert.h>
#include <math.h>
#include <stdlib.h>

int main(void) {
    app_rate_t state = {0};
    app_rate_config_t config = {.period_us = 1000};
    for (unsigned i = 0; i < 3; ++i) {
        config.axis[i] = (rate_axis_config_t){1, 0, 0, 0.5f, 1, 0};
        config.max_rate_radps[i] = 2;
    }
    app_receiver_sample_t rc = {.connected = true};
    for (unsigned i = 0; i < CRSF_CHANNEL_COUNT; ++i)
        rc.channels[i] = 992;
    rc.channels[0] = 1811;
    app_command_t command;
    assert(app_command_decode(&rc, &command));
    float gyro[3] = {1.75f, 0.25f, -0.125f};
    assert(!app_rate_step(&state, &command, gyro, 0));
    assert(app_rate_configure(&state, &config));
    /* Wrapped timestamps and FLU -> FRD signs, with decoded full roll. */
    assert(!app_rate_step(&state, &command, gyro, UINT32_MAX - 499U));
    assert(app_rate_step(&state, &command, gyro, 500));
    assert(abs(state.mixer.achieved.roll - NORM_Q15_ONE / 4) <= 2);
    assert(abs(state.mixer.achieved.pitch - NORM_Q15_ONE / 4) <= 2);
    assert(abs(state.mixer.achieved.yaw + NORM_Q15_ONE / 8) <= 2);
    assert(state.valid);
    assert(!app_rate_step(&state, &command, gyro, 20501));
    assert(!state.have_time && !state.valid && state.mixer.motor[0] == 0);
    assert(!app_rate_step(&state, &command, gyro, 21000));
    assert(app_rate_step(&state, &command, gyro, 22000));
    command.valid = false;
    assert(!app_rate_step(&state, &command, gyro, 23000));
    assert(!state.controller.initialized && state.residual[0] == 0);
    command.valid = true;
    gyro[2] = NAN;
    assert(!app_rate_step(&state, &command, gyro, 24000));
    gyro[2] = 0;
    /* Allocator saturation is fed back and blocks further I growth. */
    command.roll = command.pitch = command.yaw = NORM_Q15_ONE;
    for (unsigned i = 0; i < 3; ++i) {
        config.axis[i].kp = 0.4f;
        config.axis[i].ki = 1;
        gyro[i] = 0;
    }
    assert(app_rate_configure(&state, &config));
    assert(!app_rate_step(&state, &command, gyro, 0));
    assert(app_rate_step(&state, &command, gyro, 1000));
    assert(state.residual[0] > 0 && state.residual[2] > 0);
    float integral = state.controller.integral[0];
    assert(integral > 0);
    assert(app_rate_step(&state, &command, gyro, 2000));
    assert(state.controller.integral[0] == integral);
    config.period_us = 0;
    assert(!app_rate_configure(&state, &config));
    config.period_us = 20001;
    assert(!app_rate_configure(&state, &config));
    config.period_us = 1000;
    config.max_rate_radps[2] = NAN;
    assert(!app_rate_configure(&state, &config));
    assert(!state.configured && !state.valid);
    return 0;
}
