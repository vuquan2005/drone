#ifndef IMU_CALIBRATION_STORE_H
#define IMU_CALIBRATION_STORE_H

#include "app_imu.h"

/** Load the newest valid record for the given chip ID, board map and ranges.
 * A v1 record supplies gyro bias and disables accel correction. Output must
 * be non-NULL. False leaves it untouched. Serialized foreground only.
 */
bool imu_calibration_load(uint8_t chip_id, app_imu_calibration_t *calibration);

/** Save a complete gyro and accelerometer calibration as v2.
 * Input must be non-NULL. Returns false on invalid data or flash failure.
 * Preserves the previous compatible copy until the new copy is finalized.
 * Blocking foreground only. Motor outputs must be disabled.
 */
bool imu_calibration_save(uint8_t chip_id,
                          const app_imu_calibration_t *calibration);

#endif
