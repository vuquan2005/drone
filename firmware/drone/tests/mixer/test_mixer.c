#include "mixer.h"
#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static float real(norm_q15_t value) {
    return (float)value / NORM_Q15_ONE;
}

static void near(float actual, float expected, float tolerance) {
    if (fabsf(actual - expected) > tolerance) {
        fprintf(stderr, "actual %.9g expected %.9g tolerance %.9g\n", actual,
                expected, tolerance);
    }
    assert(fabsf(actual - expected) <= tolerance);
}

/* Continuous reference allocation, independent of fixed-point rounding. */
static void reference(const mixer_command_t *cmd, float motor[4]) {
    const float divisor = fmaxf(1.0f, fabsf(cmd->roll) + fabsf(cmd->pitch));
    const float r = cmd->roll / divisor / 2;
    const float p = cmd->pitch / divisor / 2;
    const float rp[] = {-r + p, -r - p, r - p, r + p};
    const float y[] = {cmd->yaw / 2, -cmd->yaw / 2, cmd->yaw / 2,
                       -cmd->yaw / 2};
    float fraction = 1.0f;
    for (unsigned i = 0; i < 4; ++i) {
        for (unsigned j = 0; j < 4; ++j) {
            if (y[i] > y[j]) {
                fraction = fminf(fraction, (1 - rp[i] + rp[j]) / (y[i] - y[j]));
            }
        }
    }
    float lo = 0, hi = 0;
    for (unsigned i = 0; i < 4; ++i) {
        motor[i] = rp[i] + y[i] * fraction;
        lo = fminf(lo, motor[i]);
        hi = fmaxf(hi, motor[i]);
    }
    const float collective = fminf(fmaxf(cmd->throttle, -lo), 1 - hi);
    for (unsigned i = 0; i < 4; ++i)
        motor[i] += collective;
}

static void check(const mixer_command_t *cmd) {
    mixer_result_t out;
    assert(mixer_quad_x(cmd, &out));
    const norm_q15_t *m = out.motor;
    float expected[4];
    reference(cmd, expected);
    for (unsigned i = 0; i < 4; ++i) {
        assert(m[i] >= 0 && m[i] <= NORM_Q15_ONE);
        near(real(m[i]), expected[i], 8.0f / NORM_Q15_ONE);
    }
    const norm_q15_t t = (m[0] + m[1] + m[2] + m[3]) / 4;
    const norm_q15_t r = (-m[0] - m[1] + m[2] + m[3]) / 2;
    const norm_q15_t p = (m[0] - m[1] - m[2] + m[3]) / 2;
    const norm_q15_t y = (m[0] - m[1] + m[2] - m[3]) / 2;
    assert(out.achieved.throttle == t);
    assert(out.achieved.roll == r);
    assert(out.achieved.pitch == p);
    assert(out.achieved.yaw == y);
    assert(out.residual.throttle ==
           (norm_q15_t)roundf(cmd->throttle * NORM_Q15_ONE) - t);
    assert(out.residual.roll ==
           (norm_q15_t)roundf(cmd->roll * NORM_Q15_ONE) - r);
    assert(out.residual.pitch ==
           (norm_q15_t)roundf(cmd->pitch * NORM_Q15_ONE) - p);
    assert(out.residual.yaw == (norm_q15_t)roundf(cmd->yaw * NORM_Q15_ONE) - y);
    assert(out.roll_pitch_scale > 0 && out.roll_pitch_scale <= NORM_Q15_ONE);
    assert(out.yaw_scale >= 0 && out.yaw_scale <= NORM_Q15_ONE);
    const float divisor = fmaxf(1.0f, fabsf(cmd->roll) + fabsf(cmd->pitch));
    near(real(r), cmd->roll / divisor, 5.0f / NORM_Q15_ONE);
    near(real(p), cmd->pitch / divisor, 5.0f / NORM_Q15_ONE);
    near(real(y), cmd->yaw * real(out.yaw_scale), 4.0f / NORM_Q15_ONE);
    if (out.yaw_scale < NORM_Q15_ONE) {
        norm_q15_t lo = m[0], hi = m[0];
        for (unsigned i = 1; i < 4; ++i) {
            if (m[i] < lo)
                lo = m[i];
            if (m[i] > hi)
                hi = m[i];
        }
        assert(hi - lo >= NORM_Q15_ONE - 4);
    }
}

static uint32_t random_state = UINT32_C(0xa5c37b19);
static uint32_t random_u32(void) {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

int main(void) {
    for (int t = 0; t <= 10; ++t) {
        for (int r = -10; r <= 10; ++r) {
            for (int p = -10; p <= 10; ++p) {
                for (int y = -10; y <= 10; ++y) {
                    const mixer_command_t cmd = {t / 10.0f, r / 10.0f,
                                                 p / 10.0f, y / 10.0f};
                    check(&cmd);
                }
            }
        }
    }
    /* Deterministic samples include fractional Q15 inputs and tiny commands. */
    for (unsigned i = 0; i < 50000; ++i) {
        mixer_command_t cmd = {
            (random_u32() % 131073U) / 131072.0f,
            ((int32_t)(random_u32() % 262145U) - 131072) / 131072.0f,
            ((int32_t)(random_u32() % 262145U) - 131072) / 131072.0f,
            ((int32_t)(random_u32() % 262145U) - 131072) / 131072.0f};
        check(&cmd);
        cmd.yaw /= NORM_Q15_ONE;
        check(&cmd);
    }
    const float edges[] = {0,
                           0.5f / NORM_Q15_ONE,
                           1.0f / NORM_Q15_ONE,
                           1.5f / NORM_Q15_ONE,
                           0.5f,
                           1.0f - 0.5f / NORM_Q15_ONE,
                           1};
    for (unsigned i = 0; i < sizeof(edges) / sizeof(edges[0]); ++i) {
        for (int sign = -1; sign <= 1; sign += 2) {
            const mixer_command_t cmd = {edges[i], sign * edges[i],
                                         -sign * edges[i], sign * edges[i]};
            check(&cmd);
        }
    }
    mixer_result_t out;
    mixer_command_t cmd = {0, 0, 0, 0};
    assert(mixer_quad_x(&cmd, &out));
    for (unsigned i = 0; i < 4; ++i)
        assert(out.motor[i] == 0);
    const float half_lsb = 0.5f / NORM_Q15_ONE;
    cmd.throttle = nextafterf(half_lsb, 0.0f);
    assert(mixer_quad_x(&cmd, &out));
    assert(out.achieved.throttle == 0);
    cmd.throttle = half_lsb;
    assert(mixer_quad_x(&cmd, &out));
    assert(out.achieved.throttle == 1);
    for (int sign = -1; sign <= 1; sign += 2) {
        cmd = (mixer_command_t){0.5f, sign * half_lsb, 0, 0};
        assert(mixer_quad_x(&cmd, &out));
        assert(out.achieved.roll + out.residual.roll == sign);
    }
    cmd = (mixer_command_t){0, 0.5f, 0, 0};
    assert(mixer_quad_x(&cmd, &out));
    assert(out.achieved.throttle == NORM_Q15_ONE / 4);
    assert(out.residual.throttle == -NORM_Q15_ONE / 4);
    cmd = (mixer_command_t){1, 0.5f, 0, 0};
    assert(mixer_quad_x(&cmd, &out));
    assert(out.achieved.throttle == 3 * NORM_Q15_ONE / 4);
    assert(out.residual.throttle == NORM_Q15_ONE / 4);
    assert(!mixer_quad_x(NULL, &out));
    assert(!mixer_quad_x(&cmd, NULL));
    const mixer_result_t zero = {0};
    assert(memcmp(&out, &zero, sizeof(out)) == 0);
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
            memset(&out, 0x55, sizeof(out));
            assert(!mixer_quad_x(&cmd, &out));
            assert(memcmp(&out, &zero, sizeof(out)) == 0);
        }
    }
    cmd = (mixer_command_t){-0.01f, 0, 0, 0};
    assert(!mixer_quad_x(&cmd, &out));
    puts("Mixer: 101871 grid, 100000 seeded and boundary requests passed (8 "
         "Q15 LSB reference tolerance)");
    return 0;
}
