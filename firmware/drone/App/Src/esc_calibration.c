#include "esc_calibration.h"
#include "app_log.h"
#include "app_safety.h"
#include "bsp_motor_pwm.h"
#include "main.h"
#include <stddef.h>

static esc_calibration_state_t state;
static esc_calibration_reason_t reason;
static uint32_t phase_start_ms;

static void transition(esc_calibration_state_t next,
                       esc_calibration_reason_t cause) {
    state = next;
    reason = cause;
    phase_start_ms = HAL_GetTick();
    APP_LOG_INFO("esc_calib", "state=%u reason=%u", (unsigned)state,
                 (unsigned)reason);
}

static void pwm_error(void) {
    bsp_motor_pwm_stop();
    transition(ESC_CALIBRATION_PWM_ERROR, ESC_CALIBRATION_PWM_FAILED);
}

static bool set_pulse(uint16_t pulse_us) {
    uint16_t pulses[BSP_MOTOR_PWM_MOTOR_COUNT];
    for (unsigned i = 0; i < BSP_MOTOR_PWM_MOTOR_COUNT; ++i) {
        pulses[i] = pulse_us;
    }
    if (!bsp_motor_pwm_set_all_us(pulses)) {
        pwm_error();
        return false;
    }
    return true;
}

static void minimum(esc_calibration_state_t next,
                    esc_calibration_reason_t cause) {
    if (set_pulse(BSP_MOTOR_PWM_MIN_PULSE_US)) {
        transition(next, cause);
    }
}

void esc_calibration_init(void) {
    if (!bsp_motor_pwm_init()) {
        pwm_error();
        return;
    }
    transition(ESC_CALIBRATION_READY, ESC_CALIBRATION_OK);
}

esc_calibration_state_t esc_calibration_get_state(void) {
    return state;
}

esc_calibration_reason_t esc_calibration_get_reason(void) {
    return reason;
}

void esc_calibration_process(const app_command_t *command) {
    if (state != ESC_CALIBRATION_READY && state != ESC_CALIBRATION_MAX &&
        state != ESC_CALIBRATION_MIN) {
        return;
    }
    esc_calibration_reason_t failure = ESC_CALIBRATION_OK;
    if (command == NULL || !command->valid) {
        failure = ESC_CALIBRATION_RC_LOST;
    } else if (command->arm_requested) {
        failure = ESC_CALIBRATION_ARM_REQUESTED;
    } else if (command->throttle > APP_ARM_THROTTLE_MAX) {
        failure = ESC_CALIBRATION_THROTTLE_HIGH;
    }
    if (failure != ESC_CALIBRATION_OK) {
        minimum(ESC_CALIBRATION_ABORT_MIN, failure);
        return;
    }
    const uint32_t elapsed_ms = (uint32_t)(HAL_GetTick() - phase_start_ms);
    switch (state) {
    case ESC_CALIBRATION_READY:
        if (!command->esc_calibration_requested) {
            minimum(ESC_CALIBRATION_ABORT_MIN, ESC_CALIBRATION_CANCELLED);
        } else if (set_pulse(BSP_MOTOR_PWM_MAX_PULSE_US)) {
            transition(ESC_CALIBRATION_MAX, ESC_CALIBRATION_OK);
        }
        break;
    case ESC_CALIBRATION_MAX:
        if (elapsed_ms >= ESC_CALIBRATION_MAX_HOLD_MS) {
            minimum(ESC_CALIBRATION_ABORT_MIN, ESC_CALIBRATION_TIMEOUT);
        } else if (!command->esc_calibration_requested) {
            minimum(ESC_CALIBRATION_MIN, ESC_CALIBRATION_OK);
        }
        break;
    case ESC_CALIBRATION_MIN:
        if (elapsed_ms >= ESC_CALIBRATION_MIN_HOLD_MS) {
            transition(ESC_CALIBRATION_DONE, ESC_CALIBRATION_OK);
        }
        break;
    default:
        break;
    }
}
