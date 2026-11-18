#include "app_safety.h"
#include "bsp_board.h"
#include "bsp_imu_flash.h"
#include "imu_calibration.h"
#include "imu_calibration_store.h"
#include <assert.h>
#include <math.h>
#include <string.h>

static const app_imu_calibration_t initial = {
    .gyro_bias_radps = {0.003f, -0.004f, 0.005f}};
static const app_imu_calibration_t replacement = {
    .gyro_bias_radps = {-0.002f, 0.003f, -0.004f}};
static uint8_t pages[BSP_IMU_FLASH_SLOT_COUNT][64];
static bool finalized[BSP_IMU_FLASH_SLOT_COUNT];
static bool interrupt_save;
static unsigned writes;
static uint32_t elapsed_us;
static bsp_imu_result_t poll_status;
static float gyro_x_radps;
static float accel_z_mps2;

bool bsp_imu_flash_read(unsigned slot, void *data, size_t size) {
    assert(slot < BSP_IMU_FLASH_SLOT_COUNT && size <= sizeof(pages[slot]));
    if (!finalized[slot]) {
        return false;
    }
    memcpy(data, pages[slot], size);
    return true;
}

bool bsp_imu_flash_write(unsigned slot, const void *data, size_t size) {
    assert(slot < BSP_IMU_FLASH_SLOT_COUNT && size <= sizeof(pages[slot]));
    ++writes;
    memcpy(pages[slot], data, interrupt_save ? size / 2U : size);
    finalized[slot] = !interrupt_save;
    return finalized[slot];
}

uint32_t HAL_GetTick(void) {
    return UINT32_MAX - 2000U + elapsed_us / 1000U;
}

const bsp_imu_config_t *bsp_board_imu_config(void) {
    return NULL;
}

bsp_imu_result_t app_imu_init(app_imu_t *imu, const bsp_imu_config_t *config) {
    assert(config == NULL);
    *imu = (app_imu_t){0};
    imu->sensor.device.chip_id = 0xD1U;
    return BSP_IMU_OK;
}

bsp_imu_result_t app_imu_poll(app_imu_t *imu) {
    assert(imu->calibration.gyro_bias_radps[0] == 0.0f);
    if (poll_status == BSP_IMU_OK) {
        imu->sample.t_read_us = UINT32_MAX - 2000000U + elapsed_us;
        imu->sample.accel_mps2[2] = accel_z_mps2;
        imu->sample.gyro_radps[0] = gyro_x_radps;
        imu->sample.gyro_radps[1] = -0.004f;
        imu->sample.gyro_radps[2] = 0.005f;
    }
    return poll_status;
}

static void expect_bias(uint8_t chip_id,
                        const app_imu_calibration_t *expected) {
    app_imu_calibration_t actual = {0};
    assert(imu_calibration_load(chip_id, &actual));
    for (unsigned axis = 0U; axis < 3U; ++axis) {
        assert(fabsf(actual.gyro_bias_radps[axis] -
                     expected->gyro_bias_radps[axis]) < 1e-7f);
    }
}

static void test_flash_recovery(void) {
    app_imu_calibration_t missing = initial;
    assert(!imu_calibration_load(0xD1U, &missing));
    assert(missing.gyro_bias_radps[0] == initial.gyro_bias_radps[0]);
    const app_imu_calibration_t invalid = {
        .gyro_bias_radps = {NAN, 0.0f, 0.0f}};
    assert(!imu_calibration_save(0xD1U, &invalid) && writes == 0U);
    assert(imu_calibration_save(0xD1U, &initial));
    assert(imu_calibration_save(0x24U, &replacement));
    expect_bias(0xD1U, &initial);
    expect_bias(0x24U, &replacement);

    interrupt_save = true;
    assert(!imu_calibration_save(0xD1U, &replacement));
    expect_bias(0xD1U, &initial);
    interrupt_save = false;
    assert(imu_calibration_save(0xD1U, &replacement));
    expect_bias(0xD1U, &replacement);
    pages[1][0] ^= 1U;
    expect_bias(0xD1U, &initial);
}

static app_command_t start_session(void) {
    elapsed_us = 0U;
    writes = 0U;
    gyro_x_radps = initial.gyro_bias_radps[0];
    accel_z_mps2 = -9.80665f;
    poll_status = BSP_IMU_OK;
    imu_calibration_init();
    return (app_command_t){.valid = true, .imu_calibration_requested = true};
}

static void run_for(const app_command_t *command, uint32_t duration_us,
                    uint32_t sample_period_us) {
    const uint32_t end_us = elapsed_us + duration_us;
    while (elapsed_us < end_us) {
        imu_calibration_process(command);
        elapsed_us += sample_period_us;
    }
}

static void test_stationary_window(void) {
    const app_command_t command = start_session();
    /* The session qualifies RC after settling, including a transient dropout.
     */
    const app_command_t disconnected = {0};
    run_for(&disconnected, 50000U, 2500U);
    run_for(&command, 150000U, 2500U);
    run_for(&disconnected, 100000U, 2500U);
    run_for(&command, 2700000U, 2500U);
    assert(writes == 0U);
    gyro_x_radps = 0.2f;
    run_for(&command, 500000U, 2500U);
    gyro_x_radps = initial.gyro_bias_radps[0];
    run_for(&command, 2000000U, 2500U);
    assert(writes == 0U);
    run_for(&command, 3000000U, 2500U);
    assert(writes == 1U);
    expect_bias(0xD1U, &initial);
}

static void test_rejected_sessions(void) {
    app_command_t command = start_session();
    run_for(NULL, 1002500U, 2500U);
    run_for(&command, 5000000U, 2500U);
    assert(writes == 0U);

    command = start_session();
    run_for(&command, 1500000U, 2500U);
    imu_calibration_process(NULL);
    run_for(&command, 5000000U, 2500U);
    assert(writes == 0U);

    command = start_session();
    run_for(&command, 1000000U, 2500U);
    command.throttle = APP_ARM_THROTTLE_MAX + 1;
    imu_calibration_process(&command);
    command.throttle = 0;
    run_for(&command, 5000000U, 2500U);
    assert(writes == 0U);

    command = start_session();
    run_for(&command, 21000000U, 5000U);
    run_for(&command, 5000000U, 2500U);
    assert(writes == 0U);

    command = start_session();
    for (; elapsed_us < 21000000U; elapsed_us += 2500U) {
        gyro_x_radps = elapsed_us % 5000U == 0U ? 0.02f : -0.02f;
        imu_calibration_process(&command);
    }
    assert(writes == 0U);

    command = start_session();
    run_for(&command, 2500000U, 2500U);
    poll_status = BSP_IMU_DRIVER_ERROR;
    imu_calibration_process(&command);
    poll_status = BSP_IMU_OK;
    run_for(&command, 5000000U, 2500U);
    assert(writes == 0U);
    expect_bias(0xD1U, &initial);
}

int main(void) {
    test_flash_recovery();
    test_stationary_window();
    test_rejected_sessions();
    return 0;
}
