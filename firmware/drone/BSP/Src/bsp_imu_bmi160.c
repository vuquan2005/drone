#include "bsp_imu.h"
#include <string.h>

#define IMU_TRANSFER_BYTES 32U
#define IMU_SPI_READ_BYTES IMU_TRANSFER_BYTES

/* Legacy Bosch callbacks have no context pointer. Bound for each public call;
 * all instances must be serialized in the foreground. */
static bsp_imu_t *active_imu;

static int8_t spi_read(uint8_t id, uint8_t reg, uint8_t *data,
                       uint16_t length) {
    (void)id;
    bsp_imu_t *imu = active_imu;
    uint8_t tx[IMU_SPI_READ_BYTES + 1U] = {0};
    uint8_t rx[sizeof(tx)] = {0};
    if (length == 0U || length > IMU_SPI_READ_BYTES) {
        return BMI160_E_COM_FAIL;
    }

    /* BMI160 returns data immediately after the address, with no dummy byte. */
    tx[0] = reg;
    HAL_GPIO_WritePin(imu->config.cs_port, imu->config.cs_pin, GPIO_PIN_RESET);
    HAL_StatusTypeDef result = HAL_SPI_TransmitReceive(imu->config.spi, tx, rx,
                                                       (uint16_t)(length + 1U),
                                                       imu->config.timeout_ms);
    HAL_GPIO_WritePin(imu->config.cs_port, imu->config.cs_pin, GPIO_PIN_SET);
    if (result != HAL_OK) {
        return BMI160_E_COM_FAIL;
    }
    memcpy(data, &rx[1], length);
    return BMI160_OK;
}

static int8_t spi_write(uint8_t id, uint8_t reg, uint8_t *data,
                        uint16_t length) {
    (void)id;
    bsp_imu_t *imu = active_imu;
    uint8_t tx[IMU_TRANSFER_BYTES + 1U];
    if (length == 0U || length > IMU_TRANSFER_BYTES) {
        return BMI160_E_COM_FAIL;
    }
    tx[0] = reg;
    memcpy(&tx[1], data, length);
    HAL_GPIO_WritePin(imu->config.cs_port, imu->config.cs_pin, GPIO_PIN_RESET);
    HAL_StatusTypeDef result = HAL_SPI_Transmit(
        imu->config.spi, tx, (uint16_t)(length + 1U), imu->config.timeout_ms);
    HAL_GPIO_WritePin(imu->config.cs_port, imu->config.cs_pin, GPIO_PIN_SET);
    return result == HAL_OK ? BMI160_OK : BMI160_E_COM_FAIL;
}

static void delay_ms(uint32_t period) {
    /* Split long waits to avoid overflow in the millisecond-to-us conversion.
     */
    while (period-- > 0U) {
        active_imu->config.delay_us(1000U);
    }
}

bsp_imu_result_t bsp_imu_init(bsp_imu_t *imu, const bsp_imu_config_t *config) {
    if (imu == NULL) {
        return BSP_IMU_INVALID_ARGUMENT;
    }
    /* Copy before resetting, so reinitialization with &imu->config is valid. */
    bsp_imu_config_t bus = {0};
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
    active_imu = imu;
    imu->device.intf = BMI160_SPI_INTF;
    imu->device.read = spi_read;
    imu->device.write = spi_write;
    imu->device.delay_ms = delay_ms;
    imu->device.read_write_len = IMU_TRANSFER_BYTES;
    HAL_GPIO_WritePin(bus.cs_port, bus.cs_pin, GPIO_PIN_SET);
    bus.delay_us(2000U);

    int8_t result = bmi160_init(&imu->device);
    if (result == BMI160_OK) {
        imu->device.accel_cfg = (struct bmi160_cfg){
            .odr = BMI160_ACCEL_ODR_400HZ,
            .range = BMI160_ACCEL_RANGE_16G,
            .bw = BMI160_ACCEL_BW_NORMAL_AVG4,
            .power = BMI160_ACCEL_NORMAL_MODE,
        };
        imu->device.gyro_cfg = (struct bmi160_cfg){
            .odr = BMI160_GYRO_ODR_400HZ,
            .range = BMI160_GYRO_RANGE_2000_DPS,
            .bw = BMI160_GYRO_BW_NORMAL_MODE,
            .power = BMI160_GYRO_NORMAL_MODE,
        };
        /* Apply ODR/range only after both sensors have left suspend. The
         * observed board retained accel's reset ODR when configured first. */
        result = bmi160_set_power_mode(&imu->device);
        if (result == BMI160_OK) {
            bus.delay_us(5000U);
            result = bmi160_set_sens_conf(&imu->device);
        }
    }
    imu->driver_error = result;
    imu->initialized = result == BMI160_OK;
    return imu->initialized ? BSP_IMU_OK : BSP_IMU_DRIVER_ERROR;
}

bsp_imu_result_t bsp_imu_read(bsp_imu_t *imu, bsp_imu_raw_sample_t *sample) {
    if (imu == NULL || sample == NULL) {
        return BSP_IMU_INVALID_ARGUMENT;
    }
    if (!imu->initialized) {
        return BSP_IMU_NOT_INITIALIZED;
    }
    /* Wait for both flags before reading data registers: a partial burst would
     * acknowledge one sensor early and could starve a combined sample. */
    active_imu = imu;
    uint8_t status = 0;
    imu->driver_error =
        bmi160_get_regs(BMI160_STATUS_ADDR, &status, 1, &imu->device);
    if (imu->driver_error != BMI160_OK) {
        return BSP_IMU_DRIVER_ERROR;
    }
    /* STATUS bits 7 (accel) and 6 (gyro) are cleared by data reads. */
    const uint8_t ready = 0xc0U;
    if ((status & ready) != ready) {
        return BSP_IMU_NO_DATA;
    }
    struct bmi160_sensor_data accel = {0}, gyro = {0};
    imu->driver_error =
        bmi160_get_sensor_data(BMI160_BOTH_ACCEL_AND_GYRO | BMI160_TIME_SEL,
                               &accel, &gyro, &imu->device);
    if (imu->driver_error != BMI160_OK) {
        return BSP_IMU_DRIVER_ERROR;
    }
    *sample = (bsp_imu_raw_sample_t){
        .accel = {accel.x, accel.y, accel.z},
        .gyro = {gyro.x, gyro.y, gyro.z},
        .sensor_time_ticks = accel.sensortime,
    };
    return BSP_IMU_OK;
}
