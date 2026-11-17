#include "app_command.h"
#include <stddef.h>

static int32_t clipped(uint16_t value) {
    if (value < APP_COMMAND_RC_MIN) {
        return APP_COMMAND_RC_MIN;
    }
    if (value > APP_COMMAND_RC_MAX) {
        return APP_COMMAND_RC_MAX;
    }
    return value;
}

static norm_q15_t stick(uint16_t raw) {
    const int32_t delta = clipped(raw) - APP_COMMAND_RC_CENTER;
    const int32_t magnitude = delta < 0 ? -delta : delta;
    if (magnitude <= APP_COMMAND_DEADBAND) {
        return 0;
    }
    const int32_t span = delta < 0 ? APP_COMMAND_RC_CENTER - APP_COMMAND_RC_MIN
                                   : APP_COMMAND_RC_MAX - APP_COMMAND_RC_CENTER;
    const int32_t denominator = span - APP_COMMAND_DEADBAND;
    const norm_q15_t result =
        ((magnitude - APP_COMMAND_DEADBAND) * NORM_Q15_ONE + denominator / 2) /
        denominator;
    return delta < 0 ? -result : result;
}

static app_command_switch_t switch_position(uint16_t raw) {
    if (raw <= (APP_COMMAND_RC_MIN + APP_COMMAND_RC_CENTER) / 2U) {
        return APP_COMMAND_SWITCH_LOW;
    }
    if (raw >= (APP_COMMAND_RC_CENTER + APP_COMMAND_RC_MAX + 1U) / 2U) {
        return APP_COMMAND_SWITCH_HIGH;
    }
    return APP_COMMAND_SWITCH_MIDDLE;
}

bool app_command_decode(const app_receiver_sample_t *sample,
                        app_command_t *command) {
    if (command == NULL) {
        return false;
    }
    *command = (app_command_t){0};
    if (sample == NULL || !sample->connected) {
        return false;
    }
    for (unsigned i = 0; i < 8U; ++i) {
        if (sample->channels[i] > 2047U) {
            return false;
        }
    }
    command->roll = stick(sample->channels[0]);
    command->pitch = stick(sample->channels[1]);
    command->yaw = stick(sample->channels[3]);
    const int32_t span = APP_COMMAND_RC_MAX - APP_COMMAND_RC_MIN;
    command->throttle =
        ((clipped(sample->channels[2]) - APP_COMMAND_RC_MIN) * NORM_Q15_ONE +
         span / 2) /
        span;
    command->arm_requested =
        switch_position(sample->channels[4]) == APP_COMMAND_SWITCH_HIGH;
    command->mode = switch_position(sample->channels[5]);
    command->esc_calibration_requested =
        switch_position(sample->channels[6]) == APP_COMMAND_SWITCH_HIGH;
    switch (switch_position(sample->channels[7])) {
    case APP_COMMAND_SWITCH_LOW:
        command->telemetry_mode = APP_COMMAND_TELEMETRY_OFF;
        break;
    case APP_COMMAND_SWITCH_MIDDLE:
        command->telemetry_mode = APP_COMMAND_TELEMETRY_RECORD;
        break;
    case APP_COMMAND_SWITCH_HIGH:
        command->telemetry_mode = APP_COMMAND_TELEMETRY_STREAM;
        break;
    }
    command->last_frame_ms = sample->last_frame_ms;
    command->valid = true;
    return true;
}
