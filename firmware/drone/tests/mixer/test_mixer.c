#include "mixer.h"
#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>

static void near(float actual, float expected) {
    assert(fabsf(actual - expected) < 2e-6f);
}

int main(void) {
    mixer_result_t out;
    mixer_command_t cmd = {0.5f, 0.2f, 0.0f, 0.0f};
    assert(mixer_quad_x(&cmd, &out));
    near(out.motor[0], 0.4f);
    near(out.motor[2], 0.6f);
    near(out.motor[3], 0.6f);
    near(out.motor[1], 0.4f);
    cmd = (mixer_command_t){0.5f, 0, 0.2f, 0};
    assert(mixer_quad_x(&cmd, &out));
    near(out.motor[0], 0.6f);
    near(out.motor[2], 0.4f);
    near(out.motor[3], 0.6f);
    near(out.motor[1], 0.4f);
    cmd = (mixer_command_t){0.5f, 0, 0, 0.2f};
    assert(mixer_quad_x(&cmd, &out));
    near(out.motor[0], 0.6f);
    near(out.motor[2], 0.6f);
    near(out.motor[3], 0.4f);
    near(out.motor[1], 0.4f);

    /* Exhaustively cover a signed command grid, including saturation corners.
     * Reconstruct moments from motor outputs independently of mixer metadata.
     */
    for (int t = 0; t <= 10; ++t) {
        for (int r = -10; r <= 10; ++r) {
            for (int p = -10; p <= 10; ++p) {
                for (int y = -10; y <= 10; ++y) {
                    cmd = (mixer_command_t){t / 10.0f, r / 10.0f, p / 10.0f,
                                            y / 10.0f};
                    assert(mixer_quad_x(&cmd, &out));
                    const float *m = out.motor;
                    for (unsigned i = 0; i < MIXER_MOTOR_COUNT; ++i) {
                        assert(isfinite(m[i]) && m[i] >= 0 && m[i] <= 1);
                    }
                    const float roll = (-m[0] - m[1] + m[2] + m[3]) / 2;
                    const float pitch = (m[0] - m[1] - m[2] + m[3]) / 2;
                    const float yaw = (m[0] - m[1] + m[2] - m[3]) / 2;
                    const float scale =
                        1.0f / fmaxf(1.0f, fabsf(cmd.roll) + fabsf(cmd.pitch));
                    near(roll, cmd.roll * scale);
                    near(pitch, cmd.pitch * scale);
                    near(out.achieved.roll, roll);
                    near(out.achieved.pitch, pitch);
                    near(out.achieved.yaw, yaw);
                    near(out.achieved.throttle,
                         (m[0] + m[1] + m[2] + m[3]) / 4);
                    near(out.residual.roll, cmd.roll - roll);
                    near(out.residual.pitch, cmd.pitch - pitch);
                    near(out.residual.yaw, cmd.yaw - yaw);
                    near(out.residual.throttle,
                         cmd.throttle - out.achieved.throttle);
                    assert(out.yaw_scale >= 0 && out.yaw_scale <= 1);
                    near(yaw, cmd.yaw * out.yaw_scale);
                    /* A reduced yaw must already exhaust the available span. */
                    if (out.yaw_scale < 0.999f) {
                        float lo = m[0], hi = m[0];
                        for (unsigned i = 1; i < MIXER_MOTOR_COUNT; ++i) {
                            lo = fminf(lo, m[i]);
                            hi = fmaxf(hi, m[i]);
                        }
                        near(hi - lo, 1.0f);
                    }
                }
            }
        }
    }
    cmd = (mixer_command_t){0, 0, 0, 0};
    assert(mixer_quad_x(&cmd, &out));
    for (unsigned i = 0; i < MIXER_MOTOR_COUNT; ++i)
        near(out.motor[i], 0);
    assert(!mixer_quad_x(NULL, &out));
    assert(!mixer_quad_x(&cmd, NULL));
    const float invalid[] = {NAN, INFINITY, -INFINITY, 1.01f, -1.01f};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        for (unsigned axis = 0; axis < 4; ++axis) {
            cmd = (mixer_command_t){0.5f, 0, 0, 0};
            switch (axis) {
            case 0:
                cmd.throttle = invalid[i];
                break;
            case 1:
                cmd.roll = invalid[i];
                break;
            case 2:
                cmd.pitch = invalid[i];
                break;
            default:
                cmd.yaw = invalid[i];
                break;
            }
            assert(!mixer_quad_x(&cmd, &out));
            for (unsigned j = 0; j < MIXER_MOTOR_COUNT; ++j)
                near(out.motor[j], 0);
        }
    }
    cmd = (mixer_command_t){-0.01f, 0, 0, 0};
    assert(!mixer_quad_x(&cmd, &out));
    puts("Mixer: axis signs, 101871 grid requests and invalid inputs passed");
    return 0;
}
