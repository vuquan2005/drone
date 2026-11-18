#ifndef IMU_CALIBRATION_STORE_H
#define IMU_CALIBRATION_STORE_H

#include "app_imu.h"

/** Load the newest valid bias for the given chip ID and current board map and
 * gyro range. Output must be non-NULL. Returns false and leaves it untouched
 * if no compatible record exists. Serialized foreground only.
 */
bool imu_calibration_load(uint8_t chip_id, app_imu_calibration_t *calibration);

/** Save a bias for the given chip ID and current board map and gyro range.
 * Input must be non-NULL. Returns false on invalid data or flash failure.
 * Preserves the previous compatible copy until the new copy is finalized.
 * Blocking foreground only. Motor outputs must be disabled.
 */
bool imu_calibration_save(uint8_t chip_id,
                          const app_imu_calibration_t *calibration);

#endif
