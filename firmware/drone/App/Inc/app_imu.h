/**
 * @file app_imu.h
 * @brief IMU application state, polling and SI sample conversion.
 */
#ifndef APP_IMU_H
#define APP_IMU_H

#include "bsp_bmi270.h"

/** @brief Original sensor counts and uncalibrated SI sample in body FLU.
 * Body axes: X forward, Y left, Z up. Raw fields remain in sensor XYZ.
 * @note Board mapping from sensor XYZ is [X, -Y, -Z], for both sensors.
 * Positive angular velocity
 * follows the right-hand rule. At rest with Z up, accel Z is approximately +g.
 */
typedef struct {
    int16_t
        accel_raw[3]; /**< Original sensor XYZ counts, before body mapping. */
    int16_t
        gyro_raw[3]; /**< Original sensor XYZ counts, before body mapping. */
    float accel_mps2[3];        /**< Acceleration in m/s^2. */
    float gyro_radps[3];        /**< Angular velocity in rad/s. */
    uint32_t sensor_time_ticks; /**< Raw wrapping 24-bit sensor clock, not MCU
                                   time. */
    uint32_t t_read_us; /**< MCU uptime captured after a successful BSP read,
                          before conversion; wraps modulo 2^32. Not acquisition
                          time. Preserved with the sample on NO_DATA/error. */
} app_imu_sample_t;

/**
 * @brief IMU application state owned by one foreground context.
 * @note Keep at a stable address after init; do not copy or access
 * concurrently.
 */
typedef struct {
    bsp_bmi270_t sensor;
    app_imu_sample_t sample;
    bsp_imu_result_t status;
    bool sample_valid; /**< Most recent poll produced a new sample; not an age
                          check. */
    uint32_t sample_count;     /**< Successful samples since initialization. */
    uint32_t read_error_count; /**< Polls returning DRIVER_ERROR since
                                  initialization. */
} app_imu_t;

/**
 * @brief Reset samples and counters, then initialize the board IMU.
 * @param[out] imu Application state; must remain at a stable address after
 * init.
 * @param[in] config Board configuration; NULL leaves the IMU unconfigured.
 * @pre For a valid config, board peripherals and the delay provider are ready.
 * @return Result of bsp_bmi270_init, or BSP_IMU_INVALID_ARGUMENT for NULL imu.
 * @note Blocking foreground API. Reinitialization using &imu->sensor.config is
 * supported; previous samples and counters are discarded even if init fails.
 */
bsp_imu_result_t app_imu_init(app_imu_t *imu,
                              const bsp_bmi270_config_t *config);
/**
 * @brief Poll once and convert a new raw sample to SI units.
 * @param[in,out] imu Application state; initialize with app_imu_init first.
 * @return BSP_IMU_OK for a new sample, BSP_IMU_NO_DATA between samples, or the
 * initialization/read error. NULL imu returns BSP_IMU_INVALID_ARGUMENT.
 * @details For non-NULL imu, clears sample_valid before polling. On success,
 * replaces sample, sets sample_valid and increments sample_count. Otherwise,
 * preserves sample; read_error_count increments only on a driver read error.
 * @note Blocking foreground API; no ISR or concurrent access. Does not schedule
 * polling, calibrate, check sample age or retry initialization. Maps SI axes
 * to body FLU while preserving the original sensor counts in raw fields.
 */
bsp_imu_result_t app_imu_poll(app_imu_t *imu);

#endif
