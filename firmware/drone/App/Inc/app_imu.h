#ifndef APP_IMU_H
#define APP_IMU_H

#include "bsp_bmi270.h"

typedef struct {
    float accel_mps2[3];
    float gyro_radps[3];
    uint32_t sensor_time_ticks;
} app_imu_sample_t;

/* Keep at a stable address. All fields are owned by the foreground task. */
typedef struct {
    bsp_bmi270_t sensor;
    app_imu_sample_t sample;
    bsp_imu_result_t status;
    bool sample_valid; /* True only if the most recent poll returned a new
                          sample. */
    uint32_t sample_count;
    uint32_t read_error_count;
} app_imu_t;

bsp_imu_result_t app_imu_init(app_imu_t *imu,
                              const bsp_bmi270_config_t *config);
/* Poll from the foreground loop; NO_DATA is normal between new samples.
 * Units are SI, axes follow the sensor package. No calibration or rotation. */
bsp_imu_result_t app_imu_poll(app_imu_t *imu);

#endif
