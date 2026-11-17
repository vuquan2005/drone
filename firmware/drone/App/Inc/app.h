/**
 * @file app.h
 * @brief Foreground application initialization and polling entry points.
 */
#ifndef APP_H
#define APP_H

#include "app_command.h"
#include "app_rate.h"
#include "app_safety.h"

/**
 * @brief Initialize application state and attempt to initialize the board IMU.
 * @pre HAL, clocks and board peripherals have been initialized.
 * @note Call from the foreground before app_process. May block during sensor
 * initialization. Calling again resets samples and counters and retries init.
 * The current implementation stores IMU status internally without reporting it
 * to the caller; unconfigured wiring leaves the IMU inactive.
 */
void app_init(void);

/**
 * @brief Poll application modules once from the foreground loop.
 * @pre app_init has completed.
 * @note Call repeatedly from one context, never from an ISR. May block on SPI.
 * Does not schedule a fixed polling rate or recover failed IMU initialization.
 */
void app_process(void);

/** Copy the command last decoded by app_process; false only for NULL output.
 * Check sample->valid before use. Init clears command. No receiver polling or
 * age check occurs here; call after app_process in the same foreground context.
 * Nonblocking; no concurrent or ISR access. Does not grant motor permission. */
bool app_get_command(app_command_t *sample);

/** Configure the computational rate/mixer preview after app_init.
 * Initialization uses bench gains when DRONE_RATE_CONTROL is enabled (Debug and
 * Release presets), otherwise leaves it disabled. Invalid config disables it.
 * Same contract as app_rate_configure; serialized foreground access only. This
 * disarms and revokes the previous OFF token. period_us sets minimum spacing
 * between consumed IMU samples; faster IMU samples are skipped until due, using
 * the latest available sample. No catch-up steps or gyro decimation filter are
 * provided.
 */
bool app_configure_rate(const app_rate_config_t *config);

/** Copy latest mixer preview; false for NULL or no valid calculation. Output is
 * zero after reset/errors; it may contain a previous sample between IMU polls.
 * Call after app_process in the same foreground context. No motor I/O. */
bool app_get_rate_result(mixer_result_t *result);

/** Return current arm state after app_process; serialized foreground only.
 * No I/O. app_init starts DISARMED; invalid health enters FAILSAFE. */
app_arm_state_t app_get_arm_state(void);

#endif
