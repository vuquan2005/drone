#include "mixer.h"
#include "rate_controller.h"
#include <assert.h>
#include <float.h>
#include <math.h>

static void near(float a, float b) {
    assert(fabsf(a - b) < 1e-5f);
}

int main(void) {
    rate_controller_t state = {0};
    rate_axis_config_t cfg[3];
    for (unsigned i = 0; i < 3; ++i)
        cfg[i] = (rate_axis_config_t){1, 0, 0, 0.5f, 1, 0.01f};
    float target[3] = {0.2f, -0.3f, 0.4f};
    float gyro[3] = {0}, residual[3] = {0}, out[3];
    assert(
        rate_controller_step(&state, cfg, target, gyro, 0.001f, residual, out));
    for (unsigned i = 0; i < 3; ++i)
        near(out[i], target[i]);
    mixer_command_t command = {0.5f, out[0], out[1], out[2]};
    mixer_result_t mixed;
    assert(mixer_quad_x(&command, &mixed));
    assert(mixed.achieved.roll > 0 && mixed.achieved.pitch < 0 &&
           mixed.achieved.yaw > 0);

    /* D acts on gyro, not a setpoint step, and is filtered. */
    rate_controller_reset(&state);
    cfg[0] = (rate_axis_config_t){0, 0, 0.01f, 0.5f, 1, 0.01f};
    gyro[0] = 2;
    assert(
        rate_controller_step(&state, cfg, target, gyro, 0.01f, residual, out));
    near(out[0], 0);
    target[0] = 5;
    assert(
        rate_controller_step(&state, cfg, target, gyro, 0.01f, residual, out));
    near(out[0], 0);
    gyro[0] = 3;
    assert(
        rate_controller_step(&state, cfg, target, gyro, 0.01f, residual, out));
    near(out[0], -0.5f);

    /* I integrates elapsed seconds, freezes into saturation, unwinds away. */
    rate_controller_reset(&state);
    cfg[0] = (rate_axis_config_t){0, 1, 0, 0.2f, 1, 0};
    gyro[0] = 0;
    target[0] = 1;
    for (unsigned i = 0; i < 100; ++i)
        assert(rate_controller_step(&state, cfg, target, gyro, 0.01f, residual,
                                    out));
    near(out[0], 0.2f);
    residual[0] = 0.1f;
    assert(
        rate_controller_step(&state, cfg, target, gyro, 0.01f, residual, out));
    near(out[0], 0.2f);
    target[0] = -1;
    assert(
        rate_controller_step(&state, cfg, target, gyro, 0.01f, residual, out));
    near(out[0], 0.19f);
    residual[0] = 0;
    target[0] = 2;
    cfg[0].kp = 1;
    assert(
        rate_controller_step(&state, cfg, target, gyro, 0.01f, residual, out));
    near(out[0], 1);
    near(state.integral[0], 0.19f);

    /* An invalid later axis cannot partially advance state/output. */
    target[2] = NAN;
    assert(
        !rate_controller_step(&state, cfg, target, gyro, 0.01f, residual, out));
    assert(!state.initialized);
    for (unsigned i = 0; i < 3; ++i)
        near(out[i], 0);
    target[2] = 0;
    assert(!rate_controller_step(&state, cfg, target, gyro, 0, residual, out));
    assert(!rate_controller_step(&state, cfg, target, gyro, 0.021f, residual,
                                 out));
    assert(!rate_controller_step(&state, cfg, target, gyro, INFINITY, residual,
                                 out));
    cfg[0].kp = FLT_MAX;
    target[0] = FLT_MAX;
    assert(!rate_controller_step(&state, cfg, target, gyro, 0.001f, residual,
                                 out));
    assert(!state.initialized);
    return 0;
}
