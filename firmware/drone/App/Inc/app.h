/**
 * @file app.h
 * @brief Foreground application initialization and polling entry points.
 */
#ifndef APP_H
#define APP_H

#include "app_command.h"
#include "app_rate.h"
#include "app_safety.h"

/** Flight health snapshot; readiness alone does not grant motor permission. */
typedef struct {
    bool imu_ready;   /**< A successful IMU read is no older than 20000 us. */
    bool motor_ready; /**< PWM initialized and no output failure latched. */
    app_arm_state_t arm_state;
} app_status_t;

/** Initialize flight state, rate control, PWM and IMU; starts DISARMED.
 * Requires initialized board peripherals and shared logging/telemetry services
 * (normally runtime_init). Foreground only; IMU initialization may block.
 * Call once on entry to FLIGHT. Failed IMU init is not retried automatically.
 * PWM starts at minimum pulse; initialization does not grant arm permission.
 */
void app_init(void);

/** Process flight command, IMU, controller, safety and motor output once.
 * @param command Fresh normalized command supplied by runtime; NULL or invalid
 * clears the stored command. Input is copied, no pointer is retained.
 * Requires app_init; serialized foreground only, never ISR. May block on SPI.
 * Caller owns decoding and freshness checks; no receiver I/O occurs here.
 * Does not schedule a fixed polling rate or retry failed IMU initialization.
 */
void app_process(const app_command_t *command);

/** Copy flight health; false only for NULL output. Requires app_init.
 * No I/O except reading the MCU clock for IMU age. Serialized foreground only.
 * IMU readiness is false until the first successful read and after read errors
 * or timeout. Motor readiness does not imply ARMED or a valid mixer result.
 */
bool app_get_status(app_status_t *status);

/** Configure the flight rate controller and mixer after app_init.
 * Initialization uses bench gains when DRONE_RATE_CONTROL is enabled (Debug and
 * Release presets), otherwise leaves it disabled. Invalid config disables it.
 * Same contract as app_rate_configure; serialized foreground access only. This
 * disarms and revokes the previous OFF token. period_us sets minimum spacing
 * between consumed IMU samples; faster IMU samples are skipped until due, using
 * the latest available sample. No catch-up steps or gyro decimation filter are
 * provided.
 */
bool app_configure_rate(const app_rate_config_t *config);

/** Copy latest mixer result; false for NULL or no valid calculation. Output is
 * zero after reset/errors; it may contain a previous sample between IMU polls.
 * Call after app_process in the same foreground context. No motor I/O. */
bool app_get_rate_result(mixer_result_t *result);

/** Return current arm state after app_process; serialized foreground only.
 * No I/O. app_init starts DISARMED; invalid health enters FAILSAFE. */
app_arm_state_t app_get_arm_state(void);

#endif
