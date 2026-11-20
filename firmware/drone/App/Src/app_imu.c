#include "app_imu.h"
#include "app_log.h"
#include "bsp_time.h"
#include <string.h>

bsp_imu_result_t app_imu_init(app_imu_t *imu, const bsp_imu_config_t *config) {
    if (imu == NULL) {
        return BSP_IMU_INVALID_ARGUMENT;
    }
    bsp_imu_config_t bus = {0};
    if (config != NULL) {
        bus = *config;
    }
    memset(imu, 0, sizeof(*imu));
    imu->status = bsp_imu_init(&imu->sensor, config == NULL ? NULL : &bus);
    if (imu->status == BSP_IMU_OK) {
        APP_LOG_INFO("imu",
                     "configured source=requested %s odr_hz=%u "
                     "accel_range_g=%u gyro_range_dps=%u",
                     bsp_imu_startup_mode(), BSP_IMU_ODR_HZ,
                     (unsigned)BSP_IMU_ACCEL_RANGE_G,
                     (unsigned)BSP_IMU_GYRO_RANGE_DPS);
    }
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
    imu->status = bsp_imu_read(&imu->sensor, &raw);
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
        imu->sample.accel_raw[axis] = raw.accel[axis];
        imu->sample.gyro_raw[axis] = raw.gyro[axis];
        /* Board mounting: reverse X/Z to obtain body FRD.
         * Apply the sign after conversion so INT16_MIN cannot overflow. */
        const float sign = axis == 1U ? 1.0f : -1.0f;
        imu->sample.accel_mps2[axis] = raw.accel[axis] * accel_scale * sign;
        imu->sample.gyro_radps[axis] = raw.gyro[axis] * gyro_scale * sign -
                                       imu->calibration.gyro_bias_radps[axis];
    }
    if (imu->calibration.accel_valid) {
        float centered_mps2[3];
        for (unsigned axis = 0U; axis < 3U; ++axis) {
            centered_mps2[axis] = imu->sample.accel_mps2[axis] -
                                  imu->calibration.accel_bias_mps2[axis];
        }
        for (unsigned axis = 0U; axis < 3U; ++axis) {
            const float *row = imu->calibration.accel_matrix[axis];
            imu->sample.accel_mps2[axis] = row[0] * centered_mps2[0] +
                                           row[1] * centered_mps2[1] +
                                           row[2] * centered_mps2[2];
        }
    }
    imu->sample.sensor_time_ticks = raw.sensor_time_ticks;
    imu->sample.t_read_us = t_read_us;
    imu->sample_count++;
    imu->sample_valid = true;
    return BSP_IMU_OK;
}
