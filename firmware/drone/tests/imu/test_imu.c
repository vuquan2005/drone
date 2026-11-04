#include "app_imu.h"
#include "bsp_board.h"
#include "bsp_time.h"
#include "main.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

SPI_HandleTypeDef hspi1;
GPIO_TypeDef mock_gpioa;
static bool time_initialized;
static uint8_t registers[128];
static bool selected;
static bool fail_read;
static bool fail_write;
static bool fail_config;
static unsigned transactions;
static unsigned uploaded_bytes;
static uint8_t uploaded_config[8192];
static uint64_t delayed_us;

void bsp_time_init(void) {
    time_initialized = true;
}

void bsp_time_delay_us(uint32_t period) {
    assert(time_initialized);
    delayed_us += period;
}

static void fake_delay(uint32_t period) {
    delayed_us += period;
}

static const bsp_bmi270_config_t config = {.spi = &hspi1,
                                           .cs_port = &mock_gpioa,
                                           .cs_pin = 1U << 4,
                                           .timeout_ms = 100U,
                                           .delay_us = fake_delay};

static void reset_fixture(void) {
    memset(registers, 0, sizeof(registers));
    memset(uploaded_config, 0, sizeof(uploaded_config));
    registers[BMI2_CHIP_ID_ADDR] = BMI270_CHIP_ID;
    registers[BMI2_INTERNAL_STATUS_ADDR] = 1U;
    selected = false;
    fail_read = fail_write = fail_config = false;
    transactions = uploaded_bytes = 0U;
    delayed_us = 0U;
    time_initialized = false;
}

void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state) {
    assert(port == &mock_gpioa && pin == config.cs_pin);
    if (state == GPIO_PIN_RESET) {
        assert(!selected);
        selected = true;
    } else {
        selected = false;
    }
}

HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *handle,
                                          uint8_t *tx, uint8_t *rx,
                                          uint16_t length, uint32_t timeout) {
    assert(handle == &hspi1 && selected && timeout == config.timeout_ms);
    assert(length >= 3U && length <= 34U);
    assert((tx[0] & 0x80U) != 0U);
    transactions++;
    if (fail_read) {
        return HAL_TIMEOUT;
    }
    uint8_t address = tx[0] & 0x7fU;
    rx[0] = 0x5a; /* While sending address. */
    rx[1] = 0xa5; /* BMI270 SPI dummy; must be discarded by Bosch. */
    for (unsigned i = 2U; i < length; i++) {
        assert(address + i - 2U < sizeof(registers));
        assert(tx[i] == 0U);
        rx[i] = registers[address + i - 2U];
    }
    if (address == BMI2_STATUS_ADDR && length > 3U) {
        registers[BMI2_STATUS_ADDR] &= ~(BMI2_DRDY_ACC | BMI2_DRDY_GYR);
    }
    return HAL_OK;
}

HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *handle, uint8_t *tx,
                                   uint16_t length, uint32_t timeout) {
    assert(handle == &hspi1 && selected && timeout == config.timeout_ms);
    assert(length >= 2U && length <= 33U);
    assert((tx[0] & 0x80U) == 0U);
    transactions++;
    if (fail_write) {
        return HAL_ERROR;
    }
    uint8_t address = tx[0];
    if (address == BMI2_INIT_DATA_ADDR) {
        unsigned offset = ((registers[BMI2_INIT_ADDR_1] << 4U) |
                           (registers[BMI2_INIT_ADDR_0] & 0xfU)) *
                          2U;
        assert(offset + length - 1U <= sizeof(uploaded_config));
        memcpy(&uploaded_config[offset], &tx[1], length - 1U);
        uploaded_bytes += length - 1U;
    } else if (address != BMI2_CMD_REG_ADDR) {
        assert(address + length - 1U <= sizeof(registers));
        memcpy(&registers[address], &tx[1], length - 1U);
    }
    if (address == BMI2_INIT_CTRL_ADDR && tx[1] == 1U) {
        registers[BMI2_INTERNAL_STATUS_ADDR] = fail_config ? 0U : 1U;
    }
    return HAL_OK;
}

static void set_axis(uint8_t address, int16_t value) {
    uint16_t bits = (uint16_t)value;
    registers[address] = (uint8_t)bits;
    registers[address + 1U] = (uint8_t)(bits >> 8U);
}

static void test_unconfigured_and_invalid(void) {
    reset_fixture();
    app_imu_t imu = {0};
    assert(app_imu_poll(&imu) == BSP_IMU_NOT_INITIALIZED);
    assert(app_imu_init(&imu, NULL) == BSP_IMU_NOT_CONFIGURED);
    assert(app_imu_poll(&imu) == BSP_IMU_NOT_CONFIGURED);
    assert(!imu.sample_valid && transactions == 0U);
    assert(app_imu_init(NULL, &config) == BSP_IMU_INVALID_ARGUMENT);
    assert(app_imu_poll(NULL) == BSP_IMU_INVALID_ARGUMENT);

    bsp_bmi270_config_t invalid = config;
    invalid.cs_pin = 3U;
    assert(app_imu_init(&imu, &invalid) == BSP_IMU_INVALID_ARGUMENT);
    invalid = config;
    invalid.delay_us = NULL;
    assert(app_imu_init(&imu, &invalid) == BSP_IMU_INVALID_ARGUMENT);
    invalid = config;
    invalid.spi = NULL;
    assert(app_imu_init(&imu, &invalid) == BSP_IMU_INVALID_ARGUMENT);
    assert(transactions == 0U);
}

static void test_board_binding(void) {
    reset_fixture();
    const bsp_bmi270_config_t *board = bsp_board_imu_config();
    assert(board != NULL && time_initialized);
    assert(board->spi == &hspi1);
    assert(board->cs_port == IMU_CS_GPIO_Port);
    assert(board->cs_pin == IMU_CS_Pin);
    assert(board->delay_us == bsp_time_delay_us);
    assert(transactions == 0U && delayed_us == 0U);
    app_imu_t imu;
    assert(app_imu_init(&imu, board) == BSP_IMU_OK);
    assert(imu.sensor.initialized && !selected && delayed_us >= 2000U);
    assert(uploaded_bytes == imu.sensor.device.config_size);
    registers[BMI2_STATUS_ADDR] = BMI2_DRDY_ACC | BMI2_DRDY_GYR;
    assert(app_imu_poll(&imu) == BSP_IMU_OK);
    assert(imu.sample_valid && imu.sample_count == 1U);
}

static void test_real_driver_init_and_samples(void) {
    reset_fixture();
    app_imu_t imu;
    assert(app_imu_init(&imu, &config) == BSP_IMU_OK);
    assert(imu.sensor.initialized && !selected);
    assert(imu.sensor.driver_error == BMI2_OK);
    // TODO: Assert the startup delay occurs before the first SPI transaction;
    // Bosch delays alone can satisfy this cumulative lower bound.
    assert(delayed_us >= 2000U);
    assert(uploaded_bytes == imu.sensor.device.config_size);
    assert(memcmp(uploaded_config, imu.sensor.device.config_file_ptr,
                  uploaded_bytes) == 0);
    assert((registers[BMI2_ACC_CONF_ADDR] & 0xfU) == BMI2_ACC_ODR_400HZ);
    assert((registers[BMI2_ACC_CONF_ADDR + 1U] & 3U) == BMI2_ACC_RANGE_16G);
    assert((registers[BMI2_GYR_CONF_ADDR] & 0xfU) == BMI2_GYR_ODR_400HZ);
    assert((registers[BMI2_GYR_CONF_ADDR + 1U] & 7U) == BMI2_GYR_RANGE_2000);
    // TODO: Check bandwidth and performance bits too; wrong accel filtering
    // currently passes despite the ODR/range checks.
    assert((registers[BMI2_PWR_CTRL_ADDR] & 6U) == 6U);
    assert((registers[BMI2_PWR_CONF_ADDR] & 1U) == 0U);

    assert(app_imu_poll(&imu) == BSP_IMU_NO_DATA);
    assert(!imu.sample_valid && imu.sample_count == 0U);
    registers[BMI2_STATUS_ADDR] = BMI2_DRDY_ACC;
    assert(app_imu_poll(&imu) == BSP_IMU_NO_DATA);
    assert(registers[BMI2_STATUS_ADDR] == BMI2_DRDY_ACC);
    registers[BMI2_STATUS_ADDR] = BMI2_DRDY_ACC | BMI2_DRDY_GYR;
    set_axis(BMI2_ACC_X_LSB_ADDR, 2048); /* +1 g with +/-16 g scale. */
    set_axis(BMI2_ACC_X_LSB_ADDR + 2U, -2048);
    set_axis(BMI2_ACC_X_LSB_ADDR + 4U, INT16_MIN);
    set_axis(BMI2_GYR_X_LSB_ADDR, 16384); /* +1000 deg/s. */
    set_axis(BMI2_GYR_X_LSB_ADDR + 2U, -16384);
    set_axis(BMI2_GYR_X_LSB_ADDR + 4U, INT16_MAX);
    registers[BMI2_SENSORTIME_ADDR] = 0x56;
    registers[BMI2_SENSORTIME_ADDR + 1U] = 0x34;
    registers[BMI2_SENSORTIME_ADDR + 2U] = 0x12;
    assert(app_imu_poll(&imu) == BSP_IMU_OK);
    assert(imu.sample_valid && imu.sample_count == 1U);
    assert(fabsf(imu.sample.accel_mps2[0] - 9.80665f) < 0.0001f);
    assert(fabsf(imu.sample.accel_mps2[1] + 9.80665f) < 0.0001f);
    assert(fabsf(imu.sample.accel_mps2[2] + 156.9064f) < 0.0001f);
    assert(fabsf(imu.sample.gyro_radps[0] - 17.4532925f) < 0.0001f);
    assert(fabsf(imu.sample.gyro_radps[1] + 17.4532925f) < 0.0001f);
    // TODO: Use an expected value with tolerance; doubling Z still passes.
    assert(imu.sample.gyro_radps[2] > 34.9f);
    assert(imu.sample.sensor_time_ticks == 0x123456U);
    assert(registers[BMI2_STATUS_ADDR] == 0U);

    app_imu_sample_t previous = imu.sample;
    registers[BMI2_STATUS_ADDR] = 0;
    assert(app_imu_poll(&imu) == BSP_IMU_NO_DATA);
    assert(!imu.sample_valid && imu.sample_count == 1U);
    assert(memcmp(&previous, &imu.sample, sizeof(previous)) == 0);
    assert(imu.read_error_count == 0U);
    // TODO: Inject a failure in the data burst after a successful status read;
    // this global flag only exercises failure of the first status read.
    fail_read = true;
    assert(app_imu_poll(&imu) == BSP_IMU_DRIVER_ERROR);
    assert(!selected && !imu.sample_valid && imu.read_error_count == 1U);
    assert(imu.sensor.driver_error == BMI2_E_COM_FAIL);
    assert(memcmp(&previous, &imu.sample, sizeof(previous)) == 0);
    fail_read = false;
    registers[BMI2_STATUS_ADDR] = BMI2_DRDY_ACC | BMI2_DRDY_GYR;
    assert(app_imu_poll(&imu) == BSP_IMU_OK);
    assert(imu.sample_valid && imu.sample_count == 2U);

    assert(app_imu_init(&imu, &imu.sensor.config) == BSP_IMU_OK);
    assert(imu.sample_count == 0U && !imu.sample_valid);
    assert(app_imu_init(&imu, NULL) == BSP_IMU_NOT_CONFIGURED);
    assert(!imu.sensor.initialized && !imu.sample_valid);
}

static void test_init_failures(void) {
    // TODO: Inject errors at later init transactions, especially sensor config
    // writes; ignoring their return code currently passes these early failures.
    app_imu_t imu;
    reset_fixture();
    registers[BMI2_CHIP_ID_ADDR] = 0U;
    assert(app_imu_init(&imu, &config) == BSP_IMU_DRIVER_ERROR);
    assert(imu.sensor.driver_error == BMI2_E_DEV_NOT_FOUND);
    assert(!imu.sensor.initialized && !selected && uploaded_bytes == 0U);
    reset_fixture();
    fail_read = true;
    assert(app_imu_init(&imu, &config) == BSP_IMU_DRIVER_ERROR);
    assert(imu.sensor.driver_error == BMI2_E_COM_FAIL && !selected);
    reset_fixture();
    fail_write = true;
    assert(app_imu_init(&imu, &config) == BSP_IMU_DRIVER_ERROR);
    assert(imu.sensor.driver_error == BMI2_E_COM_FAIL && !selected);
    reset_fixture();
    fail_config = true;
    assert(app_imu_init(&imu, &config) == BSP_IMU_DRIVER_ERROR);
    assert(imu.sensor.driver_error == BMI2_E_CONFIG_LOAD && !selected);
    assert(!imu.sensor.initialized);
}

int main(void) {
    test_unconfigured_and_invalid();
    test_board_binding();
    test_real_driver_init_and_samples();
    test_init_failures();
    puts("IMU tests passed (real Bosch driver, simulated SPI HAL)");
    return 0;
}
