#ifndef IMU_CALIBRATION_H
#define IMU_CALIBRATION_H

#include "app_command.h"

/** Initialize the IMU for a gyro calibration session. The runtime must have
 * stopped PWM. Call once after selecting IMU_CALIB, in the foreground.
 * Blocking on sensor initialization. Reports status through App logging.
 */
void imu_calibration_init(void);

/** Poll for a stationary window and save its bias once. Pass the current RC
 * command, or NULL when RC is unavailable. Checks RC and starts sampling
 * after the initial settle period. The session ends on success, failure or
 * cancellation. PWM stays off until reset.
 * Serialized foreground only. The final flash write blocks.
 */
void imu_calibration_process(const app_command_t *command);

#endif
