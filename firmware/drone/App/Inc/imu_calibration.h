#ifndef IMU_CALIBRATION_H
#define IMU_CALIBRATION_H

#include "app_command.h"
#include "app_config.h"

typedef enum {
    IMU_CALIBRATION_IDLE,
    IMU_CALIBRATION_GYRO,
    IMU_CALIBRATION_ACCEL,
    IMU_CALIBRATION_DONE,
    IMU_CALIBRATION_RC_LOST,
    IMU_CALIBRATION_CANCELLED,
    IMU_CALIBRATION_INIT_FAILED,
    IMU_CALIBRATION_READ_FAILED,
    IMU_CALIBRATION_TIMEOUT,
    IMU_CALIBRATION_INVALID_ACCEL,
    IMU_CALIBRATION_SAVE_FAILED,
} imu_calibration_state_t;

typedef struct {
    imu_calibration_state_t state;
    unsigned captured_faces; /**< Bit mask indexed by accel_cal_face_t. */
} imu_calibration_status_t;

/** Return the settings used to acquire IMU_CALIB samples after init. The IMU
 * reference is readable in the same foreground iteration. */
app_config_t imu_calibration_get_config(void);

/** Initialize the IMU for a gyro and accelerometer calibration. The runtime
 * must have stopped PWM. Call once after selecting IMU_CALIB, in the
 * foreground. Blocking on sensor initialization. Reports status through App
 * logging.
 */
void imu_calibration_init(void);

/** Measure gyro bias, fit accelerometer correction from six aligned faces,
 * then save both together.
 * Pass the current RC command, or NULL when RC is unavailable. Checks RC and
 * starts sampling after the initial settle period. The session ends on success,
 * failure or cancellation. PWM stays off until reset. Serialized foreground
 * only. The final flash write blocks.
 */
void imu_calibration_process(const app_command_t *command);

/** Return the current stage and captured faces. Terminal results persist
 * until reset or reinitialization. Serialized foreground only. No I/O. */
imu_calibration_status_t imu_calibration_get_status(void);

#endif
