#ifndef RATE_CONTROLLER_H
#define RATE_CONTROLLER_H

#include <stdbool.h>

#define RATE_AXIS_COUNT 3U

/** FRD roll/pitch/yaw gains. Output is dimensionless mixer axis command.
 * kp: s/rad; ki: 1/rad; kd: s^2/rad. Limits are normalized [0,1].
 * d_filter_tau_s is the derivative low-pass time constant in seconds. */
typedef struct {
    float kp, ki, kd;
    float integral_limit, output_limit;
    float d_filter_tau_s;
} rate_axis_config_t;

/** Caller-owned state; initialize to zero or call rate_controller_reset().
 * Keep separate state for each controller; no concurrent access. */
typedef struct {
    float integral[RATE_AXIS_COUNT];
    float previous_gyro[RATE_AXIS_COUNT];
    float derivative[RATE_AXIS_COUNT];
    bool initialized;
} rate_controller_t;

/** Reset dynamic state; NULL is a no-op. Nonblocking, no hardware access. */
void rate_controller_reset(rate_controller_t *state);

/** Compute three normalized mixer commands using PID with D on measurement.
 * All arrays have three elements, ordered roll/pitch/yaw in body FRD.
 * setpoint and gyro are rad/s (convert App FLU gyro to [x,-y,-z] first).
 * dt_s is actual elapsed sample time in seconds, in (0,0.02]. Caller must
 * reset on disarm, sensor errors or a sampling gap; no scheduling is provided.
 * allocation_residual is previous mixer requested-minus-achieved, converted
 * from Q15 to float; supply zero on the first step. It inhibits integration
 * further into allocator saturation. Local output saturation also inhibits I.
 * D uses a backward-Euler low-pass and has no first-sample kick.
 * All pointers must be non-NULL and distinct; config and inputs are consumed
 * synchronously. State must have been reset and remain unmodified by caller.
 * Returns false on invalid/nonfinite inputs or arithmetic overflow, resets
 * state and zeros output (when non-NULL). Nonblocking foreground API, no HAL.
 * Gains are supplied by caller; this API provides no flight-tuned defaults.
 */
bool rate_controller_step(rate_controller_t *state,
                          const rate_axis_config_t config[RATE_AXIS_COUNT],
                          const float setpoint_radps[RATE_AXIS_COUNT],
                          const float gyro_radps[RATE_AXIS_COUNT], float dt_s,
                          const float allocation_residual[RATE_AXIS_COUNT],
                          float output[RATE_AXIS_COUNT]);

#endif
