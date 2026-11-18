#ifndef ATTITUDE_CONTROLLER_H
#define ATTITUDE_CONTROLLER_H

#include <stdbool.h>

/** The correction around body X and Y and the corresponding rate request. */
typedef struct {
    float tilt_error[2]; /**< Twice the tilt-error quaternion vector. */
    float rate_radps[2]; /**< Body X and Y rate setpoints, in rad/s. */
} attitude_control_result_t;

/** @brief Convert roll and pitch targets to body FRD rates using tilt error.
 * @param body_to_ned Unit quaternion (w,x,y,z) with arbitrary heading.
 * @param roll_rad Desired roll in radians, within [-pi/2, pi/2].
 * @param pitch_rad Desired pitch in radians, within [-pi/2, pi/2].
 * @param kp_per_s Nonnegative proportional gain in 1/s.
 * @param[out] result Writable tilt error and rate setpoints.
 * @return True on success. Leaves output unchanged for invalid inputs or
 * nearly opposite tilt vectors, where the shortest correction is ambiguous.
 * @pre Quaternion points to four floats and result points to writable storage.
 * @note Yaw is independent. Caller applies rate limits. No state or I/O.
 */
bool attitude_controller_step(const float body_to_ned[4], float roll_rad,
                              float pitch_rad, float kp_per_s,
                              attitude_control_result_t *result);

#endif
