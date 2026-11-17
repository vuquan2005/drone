#ifndef APP_RATE_H
#define APP_RATE_H

#include "app_command.h"
#include "mixer.h"
#include "rate_controller.h"

/** Explicit caller-supplied gains and positive maximum body FRD rates (rad/s).
 * No flight-tuned defaults. Radio axis directions must be verified on board. */
typedef struct {
    rate_axis_config_t axis[RATE_AXIS_COUNT];
    float max_rate_radps[RATE_AXIS_COUNT];
} app_rate_config_t;

/** Foreground-owned computational pipeline; no motor permission or I/O.
 * Zero initialize before use. valid describes only the latest calculation.
 * Mixer motors are normalized thrust, not calibrated ESC commands. */
typedef struct {
    app_rate_config_t config;
    rate_controller_t controller;
    mixer_result_t mixer;
    float residual[RATE_AXIS_COUNT];
    uint32_t previous_us;
    bool configured, have_time, valid;
} app_rate_t;

/** Validate/copy configuration and reset all state. NULL/invalid config
 * disables the pipeline. Returns false on invalid input. Nonblocking,
 * foreground only; pointers must be valid and distinct. No hardware access. */
bool app_rate_configure(app_rate_t *state, const app_rate_config_t *config);

/** Clear dynamic state/output, preserving configuration; NULL is a no-op. */
void app_rate_reset(app_rate_t *state);

/** Consume one new IMU sample, with gyro in body FLU rad/s and wrapping MCU
 * read timestamp in microseconds. Call only for a new sample. Converts to FRD,
 * maps Q15 sticks linearly to configured rates, and feeds previous allocation
 * residual back to PID. First sample establishes time and returns false.
 * Invalid RC, duplicate timestamps, gaps >20 ms or calculation errors reset
 * dynamics/output and return false. Caller resets on IMU errors and sampling
 * timeout, including while no new sample arrives. arm_requested is not used:
 * this computes a preview and never arms or writes motors. NULL inputs return
 * false and reset a non-NULL state. Inputs must be distinct from state.
 * Nonblocking foreground API; serialize with configure/reset. */
bool app_rate_step(app_rate_t *state, const app_command_t *command,
                   const float gyro_flu_radps[RATE_AXIS_COUNT],
                   uint32_t time_us);

#endif
