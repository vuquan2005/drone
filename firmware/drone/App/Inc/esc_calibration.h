#ifndef ESC_CALIBRATION_H
#define ESC_CALIBRATION_H

#include "app_command.h"

/* Bench defaults, not ESC acknowledgement timings. Verify against the ESC. */
#define ESC_CALIBRATION_MAX_HOLD_MS 30000U
#define ESC_CALIBRATION_MIN_HOLD_MS 3000U

typedef enum {
    ESC_CALIBRATION_IDLE,
    ESC_CALIBRATION_READY,
    ESC_CALIBRATION_MAX,
    ESC_CALIBRATION_MIN,
    ESC_CALIBRATION_DONE,
    ESC_CALIBRATION_ABORT_MIN,
    ESC_CALIBRATION_PWM_ERROR,
} esc_calibration_state_t;

typedef enum {
    ESC_CALIBRATION_OK,
    ESC_CALIBRATION_RC_LOST,
    ESC_CALIBRATION_ARM_REQUESTED,
    ESC_CALIBRATION_THROTTLE_HIGH,
    ESC_CALIBRATION_CANCELLED,
    ESC_CALIBRATION_TIMEOUT,
    ESC_CALIBRATION_PWM_FAILED,
} esc_calibration_reason_t;

/** Initialize an exclusive calibration session, starting PWM at minimum.
 * Requires initialized HAL/TIM4 and shared logging; call once on mode entry.
 * No App/IMU/controller initialization. A fresh safe command in process starts
 * maximum PWM. Failure disables outputs and latches PWM_ERROR until reset.
 * Nonblocking foreground only. Never call while flight owns PWM.
 */
void esc_calibration_init(void);

/** Advance calibration using a fresh runtime command; NULL/invalid aborts.
 * Requires init; copies no pointers. Arm ON or throttle >2% aborts. READY
 * requires calibration ON; MAX holds 2000 us until OFF, then MIN holds 1000 us
 * for MIN_HOLD_MS before DONE. MAX_HOLD_MS expiry aborts even if OFF arrives
 * on that poll. HAL tick differences wrap modulo 2^32; poll regularly.
 * DONE/ABORT_MIN retain minimum PWM and cannot restart until reset. A queued
 * PWM write failure stops all outputs and latches PWM_ERROR. Timer hardware
 * maintains pulses between calls; this is not protection against CPU stalls.
 * No ESC acknowledgement is sensed: DONE means pulse sequence completed only.
 * Nonblocking, serialized foreground only, never ISR.
 */
void esc_calibration_process(const app_command_t *command);

/** Return session state/reason; no I/O, serialized foreground only. */
esc_calibration_state_t esc_calibration_get_state(void);
esc_calibration_reason_t esc_calibration_get_reason(void);

#endif
