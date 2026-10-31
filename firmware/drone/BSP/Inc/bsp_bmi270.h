#ifndef BSP_BMI270_H
#define BSP_BMI270_H

#include "bmi270.h"
#include "stm32f1xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BSP_IMU_OK = 0,
    BSP_IMU_NO_DATA,
    BSP_IMU_NOT_CONFIGURED,
    BSP_IMU_INVALID_ARGUMENT,
    BSP_IMU_NOT_INITIALIZED,
    BSP_IMU_DRIVER_ERROR
} bsp_imu_result_t;

/* The caller initializes SPI and configures CS as an output, initially high.
 * Use master, full duplex, 8-bit, MSB first, software NSS, SPI mode 0 or 3.
 * delay_us must provide a synchronous delay in microseconds.
 * Use from one foreground context; serialize access to a shared SPI bus. */
typedef struct {
    SPI_HandleTypeDef *spi;
    GPIO_TypeDef *cs_port;
    uint16_t cs_pin;
    uint32_t timeout_ms;
    void (*delay_us)(uint32_t period_us);
} bsp_bmi270_config_t;

/* Initial bring-up settings; these are not a flight control tuning profile. */
#define BSP_IMU_ODR_HZ 400U
#define BSP_IMU_ACCEL_RANGE_G 16.0f
#define BSP_IMU_GYRO_RANGE_DPS 2000.0f

typedef struct {
    int16_t accel[3];
    int16_t gyro[3];
    uint32_t sensor_time_ticks; /* Raw 24-bit sensor clock; wraps naturally. */
} bsp_imu_raw_sample_t;

/* Keep this instance at a stable address: device.intf_ptr points to it. */
typedef struct {
    struct bmi2_dev device;
    bsp_bmi270_config_t config;
    bool initialized;
    int8_t driver_error; /* Last Bosch return code; BMI2_OK on success. */
} bsp_bmi270_t;

bsp_imu_result_t bsp_bmi270_init(bsp_bmi270_t *imu,
                                 const bsp_bmi270_config_t *config);
/* Output is updated only when both accel and gyro are ready. */
bsp_imu_result_t bsp_bmi270_read(bsp_bmi270_t *imu,
                                 bsp_imu_raw_sample_t *sample);

#endif
