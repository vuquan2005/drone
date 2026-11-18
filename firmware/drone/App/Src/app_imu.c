#include "app_imu.h"
#include "bsp_time.h"
#include <string.h>

bsp_imu_result_t app_imu_init(app_imu_t *imu,
                              const bsp_bmi270_config_t *config) {
    if (imu == NULL) {
        return BSP_IMU_INVALID_ARGUMENT;
    }
    bsp_bmi270_config_t bus = {0};
    if (config != NULL) {
        bus = *config;
    }
    memset(imu, 0, sizeof(*imu));
    imu->status = bsp_bmi270_init(&imu->sensor, config == NULL ? NULL : &bus);
    return imu->status;
}

bsp_imu_result_t app_imu_poll(app_imu_t *imu) {
    if (imu == NULL) {
        return BSP_IMU_INVALID_ARGUMENT;
    }
    imu->sample_valid = false;
    if (!imu->sensor.initialized) {
        /* A zero-initialized object has not successfully initialized hardware.
         */
        if (imu->status == BSP_IMU_OK) {
            imu->status = BSP_IMU_NOT_INITIALIZED;
        }
        return imu->status;
    }

    bsp_imu_raw_sample_t raw;
    imu->status = bsp_bmi270_read(&imu->sensor, &raw);
    if (imu->status == BSP_IMU_DRIVER_ERROR) {
        imu->read_error_count++;
    }
    if (imu->status != BSP_IMU_OK) {
        return imu->status;
    }
    const uint32_t t_read_us = bsp_time_now_us();

    const float accel_scale = BSP_IMU_ACCEL_RANGE_G * 9.80665f / 32768.0f;
    const float gyro_scale =
        BSP_IMU_GYRO_RANGE_DPS * (3.14159265358979323846f / 180.0f) / 32768.0f;
    for (unsigned axis = 0; axis < 3U; axis++) {
        imu->sample.accel_mps2[axis] = raw.accel[axis] * accel_scale;
        imu->sample.gyro_radps[axis] = raw.gyro[axis] * gyro_scale;
    }
    imu->sample.sensor_time_ticks = raw.sensor_time_ticks;
    imu->sample.t_read_us = t_read_us;
    imu->sample_count++;
    imu->sample_valid = true;
    return BSP_IMU_OK;
}
