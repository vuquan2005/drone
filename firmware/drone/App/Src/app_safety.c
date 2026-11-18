#include "app_safety.h"
#include <stddef.h>

app_safety_reason_t app_safety_update(app_safety_t *state,
                                      const app_command_t *command,
                                      bool imu_healthy, bool control_healthy,
                                      bool pwm_healthy) {
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
        state->state = APP_FAILSAFE;
        state->reason = reason;
        state->off_seen = false;
        return reason;
    }
    if (!command->arm_requested) {
        state->state = APP_DISARMED;
        state->reason = APP_SAFETY_OK;
        state->off_seen = true;
        return APP_SAFETY_OK;
    }
    if (state->state != APP_ARMED) {
        if (!state->off_seen) {
            reason = APP_SAFETY_ARM_OFF_REQUIRED;
        } else if (command->throttle > APP_ARM_THROTTLE_MAX) {
            reason = APP_SAFETY_THROTTLE_HIGH;
        } else {
            state->state = APP_ARMED;
            state->reason = APP_SAFETY_OK;
        }
        state->off_seen = false;
    }
    return reason;
}
