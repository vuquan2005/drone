#include "app_config.h"
#include <string.h>

static void put_u16(uint8_t *out, uint16_t value) {
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8U);
}

static void put_u32(uint8_t *out, uint32_t value) {
    for (unsigned i = 0U; i < 4U; ++i)
        out[i] = (uint8_t)(value >> (8U * i));
}

static void put_float(uint8_t *out, float value) {
    _Static_assert(sizeof(float) == sizeof(uint32_t),
                   "CONFIG requires binary32");
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    put_u32(out, bits);
}

void app_config_encode(uint8_t out[APP_CONFIG_PAYLOAD_BYTES],
                       const app_config_t *config) {
    memset(out, 0, APP_CONFIG_PAYLOAD_BYTES);
    const bool imu_ready =
        config->imu != NULL && config->imu->sensor.initialized;
    out[1] = (uint8_t)config->mode;
    if (imu_ready) {
        out[0] = 1U | (config->calibration_from_flash ? 2U : 0U) |
                 (config->imu->calibration.accel_valid ? 4U : 0U);
        out[2] = config->imu->sensor.device.chip_id;
        out[3] = APP_IMU_AXIS_MAP_ID;
        put_u16(out + 4U, BSP_IMU_ODR_HZ);
        put_u16(out + 6U, BSP_IMU_ODR_HZ);
        put_u16(out + 8U, BSP_IMU_ACCEL_RANGE_G);
        put_u16(out + 10U, BSP_IMU_GYRO_RANGE_DPS);
        const bsp_imu_settings_t *imu = &config->imu->sensor.settings;
        const uint8_t codes[] = {
            imu->accel_odr,        imu->gyro_odr,        imu->accel_range,
            imu->gyro_range,       imu->accel_filter,    imu->gyro_filter,
            imu->accel_power,      imu->gyro_power,      imu->accel_filter_perf,
            imu->gyro_filter_perf, imu->gyro_noise_perf, imu->adv_power_save,
        };
        memcpy(out + 12U, codes, sizeof(codes));
        const app_imu_calibration_t *cal = &config->imu->calibration;
        for (unsigned i = 0U; i < 3U; ++i) {
            put_float(out + 24U + 4U * i, cal->gyro_bias_radps[i]);
            put_float(out + 36U + 4U * i, cal->accel_bias_mps2[i]);
            for (unsigned j = 0U; j < 3U; ++j)
                put_float(out + 48U + 4U * (3U * i + j),
                          cal->accel_matrix[i][j]);
        }
    }
    put_u32(out + 88U, config->imu_timeout_us);
    put_u16(out + 92U, config->mahony_hz);
    put_u16(out + 94U, config->attitude_hz);
    put_float(out + 96U, config->mahony_kp_per_s);
    put_float(out + 100U, config->mahony_ki_per_s2);
    put_float(out + 104U, config->attitude_kp_per_s);
    put_float(out + 108U, config->attitude_max_rad);
    put_float(out + 112U, config->gravity_min_mps2);
    put_float(out + 116U, config->gravity_max_mps2);
    if (config->rate != NULL) {
        out[0] |= 8U;
        put_u32(out + 84U, config->rate->period_us);
        for (unsigned i = 0U; i < RATE_AXIS_COUNT; ++i) {
            const rate_axis_config_t *axis = &config->rate->axis[i];
            uint8_t *row = out + 120U + 28U * i;
            put_float(row, axis->kp);
            put_float(row + 4U, axis->ki);
            put_float(row + 8U, axis->kd);
            put_float(row + 12U, axis->integral_limit);
            put_float(row + 16U, axis->output_limit);
            put_float(row + 20U, axis->d_filter_tau_s);
            put_float(row + 24U, config->rate->max_rate_radps[i]);
        }
    }
}
