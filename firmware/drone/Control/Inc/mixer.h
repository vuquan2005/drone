#ifndef MIXER_H
#define MIXER_H

#include <stdbool.h>

#define MIXER_MOTOR_COUNT 4U

/** Normalized requests, not SI force/torque. FRD body axes; thrust is upward.
 * Positive roll lowers the right side, pitch raises the nose, yaw turns right.
 * Throttle: [0,1]; each attitude axis: [-1,1]. */
typedef struct {
    float throttle;
    float roll;
    float pitch;
    float yaw;
} mixer_command_t;

/** Props-in; motor numbering runs clockwise viewed from above:
 * motor[0] = M1 front-right CCW, motor[1] = M2 rear-right CW,
 * motor[2] = M3 rear-left CCW, motor[3] = M4 front-left CW.
 * Rotation describes the propeller, not the numbering direction.
 * Each attitude coefficient has magnitude 0.5.
 * Outputs represent normalized thrust, not PWM or rotor speed. */
typedef struct {
    float motor[MIXER_MOTOR_COUNT];
    mixer_command_t achieved;
    /** Requested minus achieved; useful for controller anti-windup. */
    mixer_command_t residual;
    float roll_pitch_scale;
    float yaw_scale;
} mixer_result_t;

/** Allocate a Quad X request, prioritizing roll/pitch, then yaw, then throttle.
 * Scales roll/pitch together only if their span exceeds 1; retains the largest
 * feasible yaw fraction, then shifts collective thrust into the feasible range.
 * This policy may increase thrust at low throttle (including zero).
 * Caller owns arming, failsafe, idle, motor mapping and thrust-to-ESC
 * conversion. Stateless, nonblocking, no clock/dt required; safe with
 * independent outputs. Pointers must be non-NULL and refer to distinct valid
 * objects for this call. Returns false for NULL, nonfinite or out-of-range
 * input. On failure a non-NULL result is zeroed; zero thrust is not an ESC
 * stop/disarm instruction.
 */
bool mixer_quad_x(const mixer_command_t *command, mixer_result_t *result);

#endif
