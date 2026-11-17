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
static bool fail_data;
static unsigned transactions;
static uint64_t delayed_us;
static uint32_t read_time_us;
static unsigned clock_reads;

uint32_t bsp_time_now_us(void) {
    clock_reads++;
    return read_time_us;
}

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

static const bsp_imu_config_t config = {.spi = &hspi1,
                                        .cs_port = &mock_gpioa,
                                        .cs_pin = 1U << 4,
                                        .timeout_ms = 100U,
                                        .delay_us = fake_delay};

static void reset_fixture(void) {
    memset(registers, 0, sizeof(registers));
    registers[BMI160_CHIP_ID_ADDR] = BMI160_CHIP_ID;
    registers[BMI160_ACCEL_CONFIG_ADDR] = 0x28U;
    registers[BMI160_ACCEL_RANGE_ADDR] = BMI160_ACCEL_RANGE_2G;
    registers[BMI160_GYRO_CONFIG_ADDR] = 0x28U;
    selected = false;
    fail_read = fail_write = fail_config = fail_data = false;
    transactions = 0U;
    delayed_us = 0U;
    time_initialized = false;
    read_time_us = 0U;
    clock_reads = 0U;
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
    assert(length >= 2U && length <= 33U);
    assert(delayed_us >= 2000U);
    assert((tx[0] & 0x80U) != 0U);
    transactions++;
    if (fail_read || (fail_data && (tx[0] & 0x7fU) == BMI160_GYRO_DATA_ADDR)) {
        return HAL_TIMEOUT;
    }
    uint8_t address = tx[0] & 0x7fU;
    rx[0] = 0x5a; /* While sending address. */
    for (unsigned i = 1U; i < length; i++) {
        assert(address + i - 1U < sizeof(registers));
        assert(tx[i] == 0U);
        rx[i] = registers[address + i - 1U];
    }
    if (address == BMI160_GYRO_DATA_ADDR && length == 16U) {
        registers[BMI160_STATUS_ADDR] &= ~(0x80U | 0x40U);
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
    if (fail_config && address == BMI160_ACCEL_CONFIG_ADDR) {
        return HAL_ERROR;
    }
    if (address == BMI160_COMMAND_REG_ADDR) {
        if (tx[1] == BMI160_ACCEL_NORMAL_MODE) {
            registers[BMI160_PMU_STATUS_ADDR] |= 0x10U;
        } else if (tx[1] == BMI160_GYRO_NORMAL_MODE) {
            registers[BMI160_PMU_STATUS_ADDR] |= 0x04U;
        }
    } else {
        assert(address + length - 1U <= sizeof(registers));
        memcpy(&registers[address], &tx[1], length - 1U);
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

    bsp_imu_config_t invalid = config;
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
    const bsp_imu_config_t *board = bsp_board_imu_config();
    assert(board != NULL && time_initialized);
    assert(board->spi == &hspi1);
    assert(board->cs_port == IMU_CS_GPIO_Port);
    assert(board->cs_pin == IMU_CS_Pin);
    assert(board->delay_us == bsp_time_delay_us);
    assert(transactions == 0U && delayed_us == 0U);
    app_imu_t imu;
    assert(app_imu_init(&imu, board) == BSP_IMU_OK);
    assert(imu.sensor.initialized && !selected && delayed_us >= 2000U);
    registers[BMI160_STATUS_ADDR] = 0x80U | 0x40U;
    assert(app_imu_poll(&imu) == BSP_IMU_OK);
    assert(imu.sample_valid && imu.sample_count == 1U);
}

static void test_real_driver_init_and_samples(void) {
    reset_fixture();
    app_imu_t imu;
    assert(app_imu_init(&imu, &config) == BSP_IMU_OK);
    assert(imu.sensor.initialized && !selected);
    assert(imu.sensor.driver_error == BMI160_OK);
    assert(delayed_us >= 2000U);
    assert((registers[BMI160_ACCEL_CONFIG_ADDR] & 0xfU) ==
           BMI160_ACCEL_ODR_400HZ);
    assert((registers[BMI160_ACCEL_CONFIG_ADDR + 1U] & 0xfU) ==
           BMI160_ACCEL_RANGE_16G);
    assert((registers[BMI160_GYRO_CONFIG_ADDR] & 0xfU) ==
           BMI160_GYRO_ODR_400HZ);
    assert((registers[BMI160_GYRO_CONFIG_ADDR + 1U] & 7U) ==
           BMI160_GYRO_RANGE_2000_DPS);
    assert((registers[BMI160_ACCEL_CONFIG_ADDR] & 0xf0U) == 0x20U);
    assert((registers[BMI160_GYRO_CONFIG_ADDR] & 0x30U) == 0x20U);
    assert((registers[BMI160_PMU_STATUS_ADDR] & 0x3cU) == 0x14U);

    assert(app_imu_poll(&imu) == BSP_IMU_NO_DATA);
    assert(!imu.sample_valid && imu.sample_count == 0U);
    registers[BMI160_STATUS_ADDR] = 0x80U;
    assert(app_imu_poll(&imu) == BSP_IMU_NO_DATA);
    assert(registers[BMI160_STATUS_ADDR] == 0x80U);
    assert(clock_reads == 0U);
    read_time_us = UINT32_MAX - 5U;
    registers[BMI160_STATUS_ADDR] = 0x80U | 0x40U;
    set_axis(BMI160_ACCEL_DATA_ADDR, 2048); /* +1 g with +/-16 g scale. */
    set_axis(BMI160_ACCEL_DATA_ADDR + 2U, -2048);
    set_axis(BMI160_ACCEL_DATA_ADDR + 4U, INT16_MIN);
    set_axis(BMI160_GYRO_DATA_ADDR, 16384); /* +1000 deg/s. */
    set_axis(BMI160_GYRO_DATA_ADDR + 2U, -16384);
    set_axis(BMI160_GYRO_DATA_ADDR + 4U, INT16_MAX);
    registers[0x18U] = 0x56;
    registers[0x18U + 1U] = 0x34;
    registers[0x18U + 2U] = 0x12;
    assert(app_imu_poll(&imu) == BSP_IMU_OK);
    assert(imu.sample_valid && imu.sample_count == 1U);
    assert(fabsf(imu.sample.accel_mps2[0] - 9.80665f) < 0.0001f);
    const int16_t accel_raw[] = {2048, -2048, INT16_MIN};
    const int16_t gyro_raw[] = {16384, -16384, INT16_MAX};
    assert(memcmp(imu.sample.accel_raw, accel_raw, sizeof(accel_raw)) == 0);
    assert(memcmp(imu.sample.gyro_raw, gyro_raw, sizeof(gyro_raw)) == 0);
    assert(fabsf(imu.sample.accel_mps2[1] - 9.80665f) < 0.0001f);
    assert(fabsf(imu.sample.accel_mps2[2] - 156.9064f) < 0.0001f);
    assert(fabsf(imu.sample.gyro_radps[0] - 17.4532925f) < 0.0001f);
    assert(fabsf(imu.sample.gyro_radps[1] - 17.4532925f) < 0.0001f);
    assert(fabsf(imu.sample.gyro_radps[2] + 34.90552f) < 0.0001f);
    assert(imu.sample.sensor_time_ticks == 0x123456U);
    assert(imu.sample.t_read_us == UINT32_MAX - 5U && clock_reads == 1U);
    assert(registers[BMI160_STATUS_ADDR] == 0U);

    app_imu_sample_t previous = imu.sample;
    registers[BMI160_STATUS_ADDR] = 0;
    read_time_us = 4U; /* MCU timestamp wraps independently of sensor time. */
    assert(app_imu_poll(&imu) == BSP_IMU_NO_DATA);
    assert(!imu.sample_valid && imu.sample_count == 1U);
    assert(memcmp(&previous, &imu.sample, sizeof(previous)) == 0);
    assert(imu.read_error_count == 0U);
    assert(clock_reads == 1U);
    fail_read = true;
    assert(app_imu_poll(&imu) == BSP_IMU_DRIVER_ERROR);
    assert(!selected && !imu.sample_valid && imu.read_error_count == 1U);
    assert(imu.sensor.driver_error == BMI160_E_COM_FAIL);
    assert(memcmp(&previous, &imu.sample, sizeof(previous)) == 0);
    assert(clock_reads == 1U);
    fail_read = false;
    registers[BMI160_STATUS_ADDR] = 0x80U | 0x40U;
    assert(app_imu_poll(&imu) == BSP_IMU_OK);
    assert(imu.sample_valid && imu.sample_count == 2U);
    assert(imu.sample.t_read_us == 4U && clock_reads == 2U);

    previous = imu.sample;
    registers[BMI160_STATUS_ADDR] = 0xc0U;
    fail_data = true;
    assert(app_imu_poll(&imu) == BSP_IMU_DRIVER_ERROR);
    assert(!selected && !imu.sample_valid && imu.read_error_count == 2U);
    assert(memcmp(&previous, &imu.sample, sizeof(previous)) == 0);
    assert(clock_reads == 2U);
    fail_data = false;

    assert(app_imu_init(&imu, &imu.sensor.config) == BSP_IMU_OK);
    assert(imu.sample_count == 0U && !imu.sample_valid);
    assert(app_imu_init(&imu, NULL) == BSP_IMU_NOT_CONFIGURED);
    assert(!imu.sensor.initialized && !imu.sample_valid);
}

static void test_init_failures(void) {
    app_imu_t imu;
    reset_fixture();
    registers[BMI160_CHIP_ID_ADDR] = 0U;
    assert(app_imu_init(&imu, &config) == BSP_IMU_DRIVER_ERROR);
    assert(imu.sensor.driver_error == BMI160_E_DEV_NOT_FOUND);
    assert(!imu.sensor.initialized && !selected);
    reset_fixture();
    fail_read = true;
    assert(app_imu_init(&imu, &config) == BSP_IMU_DRIVER_ERROR);
    assert(imu.sensor.driver_error == BMI160_E_COM_FAIL && !selected);
    reset_fixture();
    fail_write = true;
    assert(app_imu_init(&imu, &config) == BSP_IMU_DRIVER_ERROR);
    assert(imu.sensor.driver_error == BMI160_E_COM_FAIL && !selected);
    reset_fixture();
    fail_config = true;
    assert(app_imu_init(&imu, &config) == BSP_IMU_DRIVER_ERROR);
    assert(imu.sensor.driver_error == BMI160_E_COM_FAIL && !selected);
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
