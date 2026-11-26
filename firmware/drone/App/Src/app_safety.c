#include "app_safety.h"
#include <stddef.h>

app_safety_reason_t app_safety_update(app_safety_t *state,
                                      const app_command_t *command,
                                      bool imu_healthy, bool control_healthy,
                                      bool pwm_healthy, uint32_t now_ms) {
    if (state == NULL)
        return APP_SAFETY_OK;
    app_safety_reason_t reason = APP_SAFETY_OK;
    if (!pwm_healthy)
        reason = APP_SAFETY_PWM;
    else if (command == NULL || !command->valid || command->throttle < 0 ||
             command->throttle > NORM_Q15_ONE)
        reason = APP_SAFETY_RC;
    else if (!imu_healthy)
        reason = APP_SAFETY_IMU;
    else if (!control_healthy)
        reason = APP_SAFETY_CONTROL;
    else if (command->esc_calibration_requested)
        reason = APP_SAFETY_CALIBRATION;
    if (reason != APP_SAFETY_OK) {
        *state = (app_safety_t){.state = APP_FAILSAFE, .reason = reason};
        return reason;
    }
    if (!command->arm_requested) {
        *state = (app_safety_t){.state = APP_DISARMED, .off_seen = true};
        return APP_SAFETY_OK;
    }
    if (state->state != APP_ARMED) {
        if (!state->off_seen)
            return APP_SAFETY_ARM_OFF_REQUIRED;
        state->off_seen = false;
        if (command->throttle > APP_ARM_THROTTLE_MAX)
            return APP_SAFETY_THROTTLE_HIGH;
        state->state = APP_ARMED;
        state->reason = APP_SAFETY_OK;
    }

    const uint32_t timeout_ms = APP_ARM_IDLE_TIMEOUT_MS;
    if (timeout_ms == 0U)
        return APP_SAFETY_OK;

    /* A throttle increase at the deadline is too late. */
    if (state->idle_active &&
        (uint32_t)(now_ms - state->idle_started_ms) >= timeout_ms) {
        *state = (app_safety_t){.state = APP_DISARMED,
                                .reason = APP_SAFETY_IDLE_TIMEOUT};
        return APP_SAFETY_IDLE_TIMEOUT;
    }
    if (command->throttle > APP_ARM_THROTTLE_MAX) {
        state->idle_active = false;
    } else if (!state->idle_active) {
        state->idle_started_ms = now_ms;
        state->idle_active = true;
    }
    return APP_SAFETY_OK;
}
