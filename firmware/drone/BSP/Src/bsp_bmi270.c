#include "bsp_bmi270.h"
#include <string.h>

#define IMU_TRANSFER_BYTES 32U
#define IMU_SPI_READ_BYTES (IMU_TRANSFER_BYTES + 1U)

static BMI2_INTF_RETURN_TYPE spi_read(uint8_t reg, uint8_t *data,
                                      uint32_t length, void *context) {
    bsp_bmi270_t *imu = context;
    uint8_t tx[IMU_SPI_READ_BYTES + 1U] = {0};
    uint8_t rx[sizeof(tx)] = {0};
    if (length == 0U || length > IMU_SPI_READ_BYTES) {
        return -1;
    }

    /* Bosch supplies the read bit and includes its dummy byte in length.
     * The byte received while transmitting the address is separate. */
    tx[0] = reg;
    HAL_GPIO_WritePin(imu->config.cs_port, imu->config.cs_pin, GPIO_PIN_RESET);
    HAL_StatusTypeDef result = HAL_SPI_TransmitReceive(imu->config.spi, tx, rx,
                                                       (uint16_t)(length + 1U),
                                                       imu->config.timeout_ms);
    HAL_GPIO_WritePin(imu->config.cs_port, imu->config.cs_pin, GPIO_PIN_SET);
    if (result != HAL_OK) {
        return -1;
    }
    memcpy(data, &rx[1], length);
    return BMI2_INTF_RET_SUCCESS;
}

static BMI2_INTF_RETURN_TYPE spi_write(uint8_t reg, const uint8_t *data,
                                       uint32_t length, void *context) {
    bsp_bmi270_t *imu = context;
    uint8_t tx[IMU_TRANSFER_BYTES + 1U];
    if (length == 0U || length > IMU_TRANSFER_BYTES) {
        return -1;
    }
    tx[0] = reg;
    memcpy(&tx[1], data, length);
    HAL_GPIO_WritePin(imu->config.cs_port, imu->config.cs_pin, GPIO_PIN_RESET);
    HAL_StatusTypeDef result = HAL_SPI_Transmit(
        imu->config.spi, tx, (uint16_t)(length + 1U), imu->config.timeout_ms);
    HAL_GPIO_WritePin(imu->config.cs_port, imu->config.cs_pin, GPIO_PIN_SET);
    return result == HAL_OK ? BMI2_INTF_RET_SUCCESS : -1;
}

static void delay_us(uint32_t period, void *context) {
    bsp_bmi270_t *imu = context;
    imu->config.delay_us(period);
}

bsp_imu_result_t bsp_bmi270_init(bsp_bmi270_t *imu,
                                 const bsp_bmi270_config_t *config) {
    if (imu == NULL) {
        return BSP_IMU_INVALID_ARGUMENT;
    }
    /* Copy before resetting, so reinitialization with &imu->config is valid. */
    bsp_bmi270_config_t bus = {0};
    if (config != NULL) {
        bus = *config;
    }
    memset(imu, 0, sizeof(*imu));
    if (config == NULL) {
        return BSP_IMU_NOT_CONFIGURED;
    }
    if (bus.spi == NULL || bus.cs_port == NULL || bus.delay_us == NULL ||
        bus.cs_pin == 0U || (bus.cs_pin & (bus.cs_pin - 1U)) != 0U ||
        bus.timeout_ms == 0U) {
        return BSP_IMU_INVALID_ARGUMENT;
    }

    imu->config = bus;
    imu->device.intf = BMI2_SPI_INTF;
    imu->device.intf_ptr = imu;
    imu->device.read = spi_read;
    imu->device.write = spi_write;
    imu->device.delay_us = delay_us;
    imu->device.read_write_len = IMU_TRANSFER_BYTES;
    HAL_GPIO_WritePin(bus.cs_port, bus.cs_pin, GPIO_PIN_SET);
    bus.delay_us(2000U);

    int8_t result = bmi270_init(&imu->device);
    if (result == BMI2_OK) {
        result = bmi2_set_adv_power_save(BMI2_DISABLE, &imu->device);
    }
    struct bmi2_sens_config settings[2] = {{.type = BMI2_ACCEL},
                                           {.type = BMI2_GYRO}};
    if (result == BMI2_OK) {
        result = bmi2_get_sensor_config(settings, 2, &imu->device);
    }
    if (result == BMI2_OK) {
        settings[0].cfg.acc.odr = BMI2_ACC_ODR_400HZ;
        settings[0].cfg.acc.range = BMI2_ACC_RANGE_16G;
        settings[0].cfg.acc.bwp = BMI2_ACC_NORMAL_AVG4;
        settings[0].cfg.acc.filter_perf = BMI2_PERF_OPT_MODE;
        settings[1].cfg.gyr.odr = BMI2_GYR_ODR_400HZ;
        settings[1].cfg.gyr.range = BMI2_GYR_RANGE_2000;
        settings[1].cfg.gyr.bwp = BMI2_GYR_NORMAL_MODE;
        settings[1].cfg.gyr.noise_perf = BMI2_PERF_OPT_MODE;
        settings[1].cfg.gyr.filter_perf = BMI2_PERF_OPT_MODE;
        result = bmi2_set_sensor_config(settings, 2, &imu->device);
    }
    if (result == BMI2_OK) {
        const uint8_t sensors[] = {BMI2_ACCEL, BMI2_GYRO};
        result = bmi2_sensor_enable(sensors, 2, &imu->device);
    }
    imu->driver_error = result;
    imu->initialized = result == BMI2_OK;
    return imu->initialized ? BSP_IMU_OK : BSP_IMU_DRIVER_ERROR;
}

bsp_imu_result_t bsp_bmi270_read(bsp_bmi270_t *imu,
                                 bsp_imu_raw_sample_t *sample) {
    if (imu == NULL || sample == NULL) {
        return BSP_IMU_INVALID_ARGUMENT;
    }
    if (!imu->initialized) {
        return BSP_IMU_NOT_INITIALIZED;
    }
    /* Wait for both flags before reading data registers: a partial burst would
     * acknowledge one sensor early and could starve a combined sample. */
    uint8_t status = 0;
    imu->driver_error = bmi2_get_status(&status, &imu->device);
    if (imu->driver_error != BMI2_OK) {
        return BSP_IMU_DRIVER_ERROR;
    }
    const uint8_t ready = BMI2_DRDY_ACC | BMI2_DRDY_GYR;
    if ((status & ready) != ready) {
        return BSP_IMU_NO_DATA;
    }
    struct bmi2_sens_data data = {0};
    imu->driver_error = bmi2_get_sensor_data(&data, &imu->device);
    if (imu->driver_error != BMI2_OK) {
        return BSP_IMU_DRIVER_ERROR;
    }
    if ((data.status & ready) != ready) {
        return BSP_IMU_NO_DATA;
    }
    *sample =
        (bsp_imu_raw_sample_t){.accel = {data.acc.x, data.acc.y, data.acc.z},
                               .gyro = {data.gyr.x, data.gyr.y, data.gyr.z},
                               .sensor_time_ticks = data.sens_time};
    return BSP_IMU_OK;
}
