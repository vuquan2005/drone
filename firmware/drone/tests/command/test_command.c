#include "app_command.h"
#include <assert.h>
#include <stddef.h>

static void cleared(const app_command_t *c) {
    assert(!c->valid && !c->arm_requested && c->last_frame_ms == 0);
    assert(c->roll == 0 && c->pitch == 0 && c->yaw == 0 && c->throttle == 0);
    assert(c->mode == APP_COMMAND_SWITCH_LOW);
    assert(!c->esc_calibration_requested && !c->imu_calibration_requested);
    assert(c->telemetry_mode == APP_COMMAND_TELEMETRY_OFF);
}

int main(void) {
    app_receiver_sample_t rc = {.connected = true, .last_frame_ms = UINT32_MAX};
    for (unsigned i = 0; i < CRSF_CHANNEL_COUNT; ++i)
        rc.channels[i] = 992;
    app_command_t c;
    assert(app_command_decode(&rc, &c));
    assert(c.valid && c.roll == 0 && c.pitch == 0 && c.yaw == 0);
    assert(c.throttle == 16394 && !c.arm_requested);
    assert(c.mode == APP_COMMAND_SWITCH_MIDDLE &&
           c.last_frame_ms == UINT32_MAX);
    rc.channels[0] = 172;
    rc.channels[1] = 1811;
    rc.channels[2] = 172;
    rc.channels[3] = 1811;
    rc.channels[4] = 1811;
    rc.channels[5] = 172;
    rc.channels[6] = 992;
    rc.channels[7] = 1811;
    assert(app_command_decode(&rc, &c));
    assert(c.roll == -NORM_Q15_ONE && c.pitch == NORM_Q15_ONE);
    assert(c.yaw == NORM_Q15_ONE && c.throttle == 0 && c.arm_requested);
    assert(c.mode == APP_COMMAND_SWITCH_LOW && !c.esc_calibration_requested);
    assert(c.telemetry_mode == APP_COMMAND_TELEMETRY_STREAM);
    norm_q15_t previous_stick = -NORM_Q15_ONE, previous_throttle = 0;
    for (unsigned raw = 0; raw <= 2047; ++raw) {
        rc.channels[0] = rc.channels[2] = (uint16_t)raw;
        assert(app_command_decode(&rc, &c));
        assert(c.roll >= previous_stick && c.roll <= NORM_Q15_ONE);
        assert(c.throttle >= previous_throttle && c.throttle <= NORM_Q15_ONE);
        if (raw <= 172)
            assert(c.roll == -NORM_Q15_ONE && c.throttle == 0);
        if (raw >= 1811)
            assert(c.roll == NORM_Q15_ONE && c.throttle == NORM_Q15_ONE);
        if (raw >= 982 && raw <= 1002)
            assert(c.roll == 0);
        if (raw == 981)
            assert(c.roll < 0);
        if (raw == 1003)
            assert(c.roll > 0);
        previous_stick = c.roll;
        previous_throttle = c.throttle;
    }
    const uint16_t values[] = {172, 582, 583, 992, 1401, 1402, 1811};
    const app_command_switch_t positions[] = {
        APP_COMMAND_SWITCH_LOW,    APP_COMMAND_SWITCH_LOW,
        APP_COMMAND_SWITCH_MIDDLE, APP_COMMAND_SWITCH_MIDDLE,
        APP_COMMAND_SWITCH_MIDDLE, APP_COMMAND_SWITCH_HIGH,
        APP_COMMAND_SWITCH_HIGH};
    for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        rc.channels[4] = rc.channels[5] = values[i];
        rc.channels[6] = rc.channels[7] = values[i];
        assert(app_command_decode(&rc, &c));
        assert(c.mode == positions[i]);
        assert(c.arm_requested == (positions[i] == APP_COMMAND_SWITCH_HIGH));
        assert(c.esc_calibration_requested ==
               (positions[i] == APP_COMMAND_SWITCH_HIGH));
        assert(!c.imu_calibration_requested);
        const app_command_telemetry_mode_t expected =
            positions[i] == APP_COMMAND_SWITCH_LOW ? APP_COMMAND_TELEMETRY_OFF
            : positions[i] == APP_COMMAND_SWITCH_MIDDLE
                ? APP_COMMAND_TELEMETRY_RECORD
                : APP_COMMAND_TELEMETRY_STREAM;
        assert(c.telemetry_mode == expected);
    }
    rc.channels[6] = rc.channels[7] = 1811;
    assert(app_command_decode(&rc, &c));
    assert(c.esc_calibration_requested &&
           c.telemetry_mode == APP_COMMAND_TELEMETRY_STREAM);
    rc.channels[7] = 992;
    assert(app_command_decode(&rc, &c));
    assert(c.esc_calibration_requested && !c.imu_calibration_requested);
    rc.channels[5] = 992;
    assert(app_command_decode(&rc, &c));
    assert(c.esc_calibration_requested && c.imu_calibration_requested &&
           c.telemetry_mode == APP_COMMAND_TELEMETRY_RECORD);
    for (unsigned i = 0; i < 8; ++i) {
        uint16_t saved = rc.channels[i];
        rc.channels[i] = 2048;
        assert(!app_command_decode(&rc, &c));
        cleared(&c);
        rc.channels[i] = saved;
    }
    rc.connected = false;
    assert(!app_command_decode(&rc, &c));
    cleared(&c);
    assert(!app_command_decode(NULL, &c));
    cleared(&c);
    assert(!app_command_decode(&rc, NULL));
    return 0;
}
