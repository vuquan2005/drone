/**
 * @file bsp_bmi270.h
 * @brief BMI270 board integration through synchronous STM32 HAL SPI.
 */
#ifndef BSP_BMI270_H
#define BSP_BMI270_H

#include "bmi270.h"
#include "stm32f1xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

/** @brief Result of an IMU initialization or read operation. */
typedef enum {
    BSP_IMU_OK = 0,           /**< Operation completed successfully. */
    BSP_IMU_NO_DATA,          /**< Both sensors are not yet ready together. */
    BSP_IMU_NOT_CONFIGURED,   /**< No board configuration was supplied. */
    BSP_IMU_INVALID_ARGUMENT, /**< A pointer or configuration field is invalid.
                               */
    BSP_IMU_NOT_INITIALIZED,  /**< Sensor initialization has not succeeded. */
    BSP_IMU_DRIVER_ERROR      /**< Bosch API failed; inspect driver_error. */
} bsp_imu_result_t;

/**
 * @brief Board resources used by the synchronous Bosch SPI callbacks.
 * @details The caller initializes SPI and configures CS as an output, initially
 * high. Use master, full duplex, 8-bit, MSB first, software NSS, SPI mode 0
 * or 3. delay_us must provide a synchronous delay in microseconds. Use from one
 * foreground context; serialize access to a shared SPI bus. The structure is
 * copied at init; referenced hardware resources and the delay provider must
 * remain valid for the lifetime of the sensor instance.
 */
typedef struct {
    SPI_HandleTypeDef *spi; /**< Initialized CubeMX SPI handle. */
    GPIO_TypeDef *cs_port;  /**< CS GPIO port, configured as an output. */
    uint16_t cs_pin;        /**< Exactly one GPIO pin bit. */
    uint32_t timeout_ms;    /**< Nonzero HAL timeout per SPI transfer, in ms. */
    void (*delay_us)(uint32_t period_us); /**< Initialized blocking us delay. */
} bsp_bmi270_config_t;

/** @brief Initial sensor output data rate in Hz, not a polling scheduler.
 * @note Keep these constants and bsp_bmi270_init settings consistent. Changing
 * the constants alone does not reconfigure the sensor.
 */
#define BSP_IMU_ODR_HZ 400U
/** @brief Positive accelerometer full-scale range in g, used for SI conversion.
 */
#define BSP_IMU_ACCEL_RANGE_G 16.0f
/** @brief Positive gyroscope full-scale range in degrees/s, used for SI
 * conversion. */
#define BSP_IMU_GYRO_RANGE_DPS 2000.0f

/** @brief Signed raw XYZ samples in sensor-package axes; no calibration. */
typedef struct {
    int16_t accel[3];
    int16_t gyro[3];
    uint32_t
        sensor_time_ticks; /**< Raw 24-bit sensor clock, not MCU time; wraps. */
} bsp_imu_raw_sample_t;

/**
 * @brief Sensor state owned by one foreground context.
 * @note Keep at a stable address after init: device.intf_ptr points to this
 * instance. Do not copy an initialized instance or access it concurrently.
 */
typedef struct {
    struct bmi2_dev device;
    bsp_bmi270_config_t config;
    bool initialized;
    int8_t
        driver_error; /**< Last Bosch return code; BMI2_OK on driver success. */
} bsp_bmi270_t;

/**
 * @brief Bind board callbacks, initialize BMI270 and enable accel and gyro.
 * @param[out] imu Sensor state; reset before validating config when non-NULL.
 * @param[in] config Board configuration copied into imu; NULL is permitted.
 * @pre For a valid config, SPI, CS GPIO and the delay provider are initialized.
 * @retval BSP_IMU_OK Sensor initialized with the bring-up settings.
 * @retval BSP_IMU_NOT_CONFIGURED config is NULL and imu is non-NULL.
 * @retval BSP_IMU_INVALID_ARGUMENT imu or a required config field is invalid.
 * @retval BSP_IMU_DRIVER_ERROR Bosch initialization or configuration failed.
 * @note Blocking foreground API. Serialize shared SPI access. Reinitialization
 * with config == &imu->config is supported. Keep imu at a stable address.
 */
bsp_imu_result_t bsp_bmi270_init(bsp_bmi270_t *imu,
                                 const bsp_bmi270_config_t *config);
/**
 * @brief Read one raw sample when both accel and gyro are ready.
 * @param[in,out] imu Sensor state, including the last Bosch error code.
 * @param[out] sample Updated only on BSP_IMU_OK; unchanged otherwise.
 * @pre imu has been successfully initialized.
 * @retval BSP_IMU_OK A new combined sample was read.
 * @retval BSP_IMU_NO_DATA Both sensors are not yet ready together.
 * @retval BSP_IMU_INVALID_ARGUMENT imu or sample is NULL.
 * @retval BSP_IMU_NOT_INITIALIZED Sensor initialization has not succeeded.
 * @retval BSP_IMU_DRIVER_ERROR Bosch read failed; inspect imu->driver_error.
 * @note Blocking foreground API; never call from an ISR. Serialize shared SPI
 * access. Reads registers directly; no FIFO buffering or automatic recovery.
 */
bsp_imu_result_t bsp_bmi270_read(bsp_bmi270_t *imu,
                                 bsp_imu_raw_sample_t *sample);

#endif
