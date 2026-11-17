/**
 * @file app.h
 * @brief Foreground application initialization and polling entry points.
 */
#ifndef APP_H
#define APP_H

#include "app_command.h"

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

#endif
