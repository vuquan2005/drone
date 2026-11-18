#ifndef ATTITUDE_CONTROLLER_H
#define ATTITUDE_CONTROLLER_H

#include <stdbool.h>

/** @brief Convert roll and pitch targets to body FRD rates using tilt error.
 * @param body_to_ned Unit quaternion (w,x,y,z) with arbitrary heading.
 * @param roll_rad Desired roll in radians, within [-pi/2, pi/2].
 * @param pitch_rad Desired pitch in radians, within [-pi/2, pi/2].
 * @param kp_per_s Nonnegative proportional gain in 1/s.
 * @param[out] tilt_rate_radps Body X and Y rate setpoints, in rad/s.
 * @return True on success. Leaves output unchanged for invalid inputs or
 * nearly opposite tilt vectors, where the shortest correction is ambiguous.
 * @pre Quaternion and output point to valid arrays of four and two elements.
 * @note Yaw is independent. Caller applies rate limits. No state or I/O.
 */
bool attitude_controller_step(const float body_to_ned[4], float roll_rad,
                              float pitch_rad, float kp_per_s,
                              float tilt_rate_radps[2]);

#endif
