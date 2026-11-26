#ifndef APP_SAFETY_H
#define APP_SAFETY_H
#include "app_command.h"

#define APP_ARM_THROTTLE_MAX (NORM_Q15_ONE / 50)
#ifndef APP_ARM_IDLE_TIMEOUT_MS
#define APP_ARM_IDLE_TIMEOUT_MS 5000U
#endif

typedef enum { APP_DISARMED, APP_ARMED, APP_FAILSAFE } app_arm_state_t;
typedef enum {
    APP_SAFETY_OK,
    APP_SAFETY_RC,
    APP_SAFETY_IMU,
    APP_SAFETY_CONTROL,
    APP_SAFETY_PWM,
    APP_SAFETY_CALIBRATION,
    APP_SAFETY_ARM_OFF_REQUIRED,
    APP_SAFETY_THROTTLE_HIGH,
    APP_SAFETY_IDLE_TIMEOUT
} app_safety_reason_t;

/** Zero-initialize in one foreground context. A healthy OFF observation permits
 * one ON edge. Startup/recovery with ON cannot arm. No HAL or I/O. */
typedef struct {
    app_arm_state_t state;
    app_safety_reason_t reason;
    uint32_t idle_started_ms;
    bool off_seen;
    bool idle_active;
} app_safety_t;

/** Evaluate once per foreground iteration with current RC and health checks.
 * NULL command is invalid RC. Invalid health enters latched FAILSAFE and
 * revokes the OFF token; recovery requires healthy OFF then ON with throttle
 * <=2%. An ON attempt with high/invalid throttle consumes the token. OFF
 * disarms. Calibration requests inhibit output. While armed, continuous
 * throttle <=2% for APP_ARM_IDLE_TIMEOUT_MS disarms and revokes the OFF token.
 * A value of zero disables the bench timeout. Caller must apply minimum PWM
 * whenever state is not ARMED and reset controller on transitions involving
 * ARMED. state must be non-NULL, initialized and distinct from command.
 * Nonblocking, serialized foreground only. Inputs are not retained.
 * @param now_ms Monotonic MCU uptime in milliseconds, wrapping modulo 2^32.
 * @return Current fault, arm rejection or idle expiry, otherwise APP_SAFETY_OK.
 * A blocked ON preserves the recorded fault or idle expiry in state->reason. */
app_safety_reason_t app_safety_update(app_safety_t *state,
                                      const app_command_t *command,
                                      bool imu_healthy, bool control_healthy,
                                      bool pwm_healthy, uint32_t now_ms);
#endif
