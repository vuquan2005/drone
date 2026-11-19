#include "accel_calibration.h"
#include "app_crc32.h"
#include "app_safety.h"
#include "app_telemetry.h"
#include "bsp_board.h"
#include "bsp_imu_flash.h"
#include "imu_calibration.h"
#include "imu_calibration_store.h"
#include <assert.h>
#include <math.h>
#include <string.h>

static const app_imu_calibration_t initial = {
    .gyro_bias_radps = {0.003f, -0.004f, 0.005f},
    .accel_bias_mps2 = {0.12f, -0.08f, 0.20f},
    .accel_scale = {1.02f, 0.98f, 1.01f},
    .accel_valid = true,
};
static const app_imu_calibration_t replacement = {
    .gyro_bias_radps = {-0.002f, 0.003f, -0.004f},
    .accel_bias_mps2 = {-0.12f, 0.2f, 0.1f},
    .accel_scale = {0.99f, 1.01f, 1.04f},
    .accel_valid = true,
};
static uint8_t pages[BSP_IMU_FLASH_SLOT_COUNT][64];
static bool finalized[BSP_IMU_FLASH_SLOT_COUNT];
static bool interrupt_save;
static unsigned writes;
static uint32_t elapsed_us;
static bsp_imu_result_t init_status = BSP_IMU_OK;
static bsp_imu_result_t poll_status;
static float gyro_x_radps;
static float accel_mps2[3];
static bool accel_vibrating;
static uint32_t imu_frames;

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
    memset(pages[slot], 0xFF, sizeof(pages[slot]));
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
    return init_status;
}

bsp_imu_result_t app_imu_poll(app_imu_t *imu) {
    assert(imu->calibration.gyro_bias_radps[0] == 0.0f);
    assert(!imu->calibration.accel_valid);
    if (poll_status == BSP_IMU_OK) {
        ++imu->sample_count;
        imu->sample.t_read_us = UINT32_MAX - 2000000U + elapsed_us;
        imu->sample.accel_raw[0] = -2345;
        imu->sample.gyro_raw[2] = 19;
        memcpy(imu->sample.accel_mps2, accel_mps2, sizeof(accel_mps2));
        if (accel_vibrating) {
            imu->sample.accel_mps2[0] +=
                elapsed_us % 5000U == 0U ? 0.1f : -0.1f;
        }
        imu->sample.gyro_radps[0] = gyro_x_radps;
        imu->sample.gyro_radps[1] = -0.004f;
        imu->sample.gyro_radps[2] = 0.005f;
    }
    return poll_status;
}

bsp_rtt_result_t app_telemetry_write_imu(const app_imu_sample_t *sample,
                                         uint32_t sequence) {
    assert(sequence == ++imu_frames);
    assert(sample->t_read_us == UINT32_MAX - 2000000U + elapsed_us);
    assert(sample->accel_raw[0] == -2345 && sample->gyro_raw[2] == 19);
    return BSP_RTT_FULL;
}

static void expect_calibration(uint8_t chip_id,
                               const app_imu_calibration_t *expected) {
    app_imu_calibration_t actual = {0};
    assert(imu_calibration_load(chip_id, &actual));
    assert(actual.accel_valid == expected->accel_valid);
    for (unsigned axis = 0U; axis < 3U; ++axis) {
        assert(fabsf(actual.gyro_bias_radps[axis] -
                     expected->gyro_bias_radps[axis]) < 1e-7f);
        if (actual.accel_valid) {
            assert(fabsf(actual.accel_bias_mps2[axis] -
                         expected->accel_bias_mps2[axis]) < 1e-5f);
            assert(fabsf(actual.accel_scale[axis] -
                         expected->accel_scale[axis]) < 1e-5f);
        }
    }
}

static void seed_legacy_page(void) {
    struct {
        uint32_t sequence;
        uint16_t version;
        uint8_t chip_id;
        uint8_t axis_map;
        uint16_t gyro_range_dps;
        uint16_t reserved;
        float bias_radps[3];
        uint32_t crc;
    } record = {1U, 1U, 0xD1U, 2U, 2000U, 0U, {0.003f, -0.004f, 0.005f}, 0U};
    assert(sizeof(record) == 28U);
    record.crc = app_crc32(&record, 24U);
    memset(pages[0], 0xFF, sizeof(pages[0]));
    memcpy(pages[0], &record, sizeof(record));
    finalized[0] = true;
}

static void test_flash_recovery(void) {
    app_imu_calibration_t missing = initial;
    assert(!imu_calibration_load(0xD1U, &missing));
    assert(missing.gyro_bias_radps[0] == initial.gyro_bias_radps[0]);
    app_imu_calibration_t invalid = initial;
    invalid.gyro_bias_radps[0] = NAN;
    assert(!imu_calibration_save(0xD1U, &invalid) && writes == 0U);
    invalid = initial;
    invalid.accel_scale[0] = 0.0f;
    assert(!imu_calibration_save(0xD1U, &invalid) && writes == 0U);
    seed_legacy_page();
    app_imu_calibration_t legacy = initial;
    legacy.accel_valid = false;
    expect_calibration(0xD1U, &legacy);
    app_imu_calibration_t loaded = initial;
    assert(imu_calibration_load(0xD1U, &loaded) && !loaded.accel_valid);

    assert(imu_calibration_save(0x24U, &replacement));
    expect_calibration(0xD1U, &legacy);
    expect_calibration(0x24U, &replacement);

    interrupt_save = true;
    assert(!imu_calibration_save(0xD1U, &replacement));
    expect_calibration(0xD1U, &legacy);
    interrupt_save = false;
    assert(imu_calibration_save(0xD1U, &replacement));
    expect_calibration(0xD1U, &replacement);
    pages[1][28] ^= 1U;
    expect_calibration(0xD1U, &legacy);
}

static void set_face(unsigned axis, int sign) {
    memcpy(accel_mps2, initial.accel_bias_mps2, sizeof(accel_mps2));
    accel_mps2[axis] += (float)sign * 9.80665f / initial.accel_scale[axis];
}

static app_command_t start_session(void) {
    elapsed_us = 0U;
    writes = 0U;
    imu_frames = 0U;
    gyro_x_radps = initial.gyro_bias_radps[0];
    set_face(2U, -1);
    accel_vibrating = false;
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

static void present_all_faces(const app_command_t *command) {
    for (unsigned axis = 0U; axis < 3U; ++axis) {
        set_face(axis, 1);
        run_for(command, 1200000U, 2500U);
        set_face(axis, -1);
        run_for(command, 1200000U, 2500U);
    }
}

static void test_combined_calibration(void) {
    const app_command_t command = start_session();
    assert(imu_calibration_get_status().state == IMU_CALIBRATION_GYRO);
    assert(imu_calibration_get_status().captured_faces == 0U);
    /* The session qualifies RC after settling, including a transient dropout.
     */
    const app_command_t disconnected = {0};
    run_for(&disconnected, 50000U, 2500U);
    run_for(&command, 150000U, 2500U);
    run_for(&disconnected, 100000U, 2500U);
    run_for(&command, 2700000U, 2500U);
    assert(writes == 0U);
    assert(imu_frames == 800U);
    gyro_x_radps = 0.2f;
    run_for(&command, 500000U, 2500U);
    gyro_x_radps = initial.gyro_bias_radps[0];
    run_for(&command, 2000000U, 2500U);
    assert(writes == 0U);
    run_for(&command, 3000000U, 2500U);
    assert(writes == 0U); /* Gyro and Z- are ready. */
    assert(imu_calibration_get_status().state == IMU_CALIBRATION_ACCEL);
    assert(imu_calibration_get_status().captured_faces ==
           (1U << ACCEL_CAL_Z_NEG));

    set_face(0U, 1);
    gyro_x_radps = 0.05f;
    run_for(&command, 1200000U, 2500U); /* Turning while aligned with X+. */
    gyro_x_radps = initial.gyro_bias_radps[0];

    accel_mps2[0] = accel_mps2[2] = 6.93f; /* Between two faces. */
    accel_mps2[1] = 0.0f;
    const uint32_t frames_before_tilt = imu_frames;
    run_for(&command, 1200000U, 2500U);
    assert(imu_frames == frames_before_tilt + 480U);

    set_face(1U, -1);
    run_for(&command, 1200000U, 2500U);
    set_face(2U, 1);
    run_for(&command, 1200000U, 2500U);
    set_face(0U, -1);
    run_for(&command, 1200000U, 2500U);
    set_face(2U, -1); /* Repeated face must not advance the count. */
    run_for(&command, 1200000U, 2500U);
    set_face(1U, 1);
    run_for(&command, 1200000U, 2500U);
    assert(writes == 0U);
    assert(imu_calibration_get_status().captured_faces ==
           (ACCEL_CAL_ALL_FACES & ~(1U << ACCEL_CAL_X_POS)));

    set_face(0U, 1);
    accel_vibrating = true;
    run_for(&command, 1002500U, 2500U);
    assert(writes == 0U);
    accel_vibrating = false;
    run_for(&command, 1200000U, 2500U);
    assert(writes == 1U);
    expect_calibration(0xD1U, &initial);
    const uint32_t frames_before_idle = imu_frames;
    run_for(&command, 1200000U, 2500U);
    assert(writes == 1U);
    assert(imu_frames == frames_before_idle);
    assert(imu_calibration_get_status().state == IMU_CALIBRATION_DONE);
    assert(imu_calibration_get_status().captured_faces == ACCEL_CAL_ALL_FACES);
}

static void test_face_angle_limit(void) {
    accel_calibration_t state = {0};
    const float gyro_radps[3] = {0.0f, 0.0f, 0.0f};
    const float tilted_mps2[3] = {0.0f, -2.0f, -9.65f};
    const float near_z_mps2[3] = {0.0f, -1.4f, -9.65f};
    for (uint32_t sample = 0U; sample <= 400U; ++sample) {
        assert(accel_calibration_push(&state, tilted_mps2, gyro_radps,
                                      sample * 2500U,
                                      380U) == ACCEL_CAL_NO_FACE);
    }
    for (uint32_t sample = 0U; sample <= 400U; ++sample) {
        const accel_cal_face_t expected =
            sample == 400U ? ACCEL_CAL_Z_NEG : ACCEL_CAL_NO_FACE;
        assert(accel_calibration_push(&state, near_z_mps2, gyro_radps,
                                      (401U + sample) * 2500U,
                                      380U) == expected);
    }
}

static void test_rejected_sessions(void) {
    init_status = BSP_IMU_DRIVER_ERROR;
    app_command_t command = start_session();
    init_status = BSP_IMU_OK;
    run_for(&command, 5000000U, 2500U);
    assert(imu_calibration_get_status().state == IMU_CALIBRATION_INIT_FAILED);
    assert(writes == 0U && imu_frames == 0U);

    command = start_session();
    run_for(NULL, 1002500U, 2500U);
    run_for(&command, 5000000U, 2500U);
    assert(writes == 0U);
    assert(imu_calibration_get_status().state == IMU_CALIBRATION_RC_LOST);

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
    assert(imu_calibration_get_status().state == IMU_CALIBRATION_CANCELLED);

    command = start_session();
    run_for(&command, 21000000U, 5000U);
    run_for(&command, 5000000U, 2500U);
    assert(writes == 0U);
    assert(imu_calibration_get_status().state == IMU_CALIBRATION_TIMEOUT);

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
    assert(imu_calibration_get_status().state == IMU_CALIBRATION_READ_FAILED);

    command = start_session();
    run_for(&command, 6000000U, 2500U); /* Gyro and Z- collected. */
    command.imu_calibration_requested = false;
    imu_calibration_process(&command);
    command.imu_calibration_requested = true;
    present_all_faces(&command);
    assert(writes == 0U);

    command = start_session();
    run_for(&command, 125000000U, 2500U);
    present_all_faces(&command);
    assert(writes == 0U);
    assert(imu_calibration_get_status().state == IMU_CALIBRATION_TIMEOUT);
    expect_calibration(0xD1U, &initial);
}

static void test_calibration_result_failures(void) {
    const app_command_t out_of_range = start_session();
    run_for(&out_of_range, 4500000U, 2500U);
    for (unsigned axis = 0U; axis < 3U; ++axis) {
        memset(accel_mps2, 0, sizeof(accel_mps2));
        accel_mps2[axis] = 0.86f * 9.80665f;
        run_for(&out_of_range, 1200000U, 2500U);
        accel_mps2[axis] = -accel_mps2[axis];
        run_for(&out_of_range, 1200000U, 2500U);
    }
    assert(imu_calibration_get_status().state == IMU_CALIBRATION_INVALID_ACCEL);
    assert(writes == 0U);

    const app_command_t save_failed = start_session();
    run_for(&save_failed, 6000000U, 2500U);
    interrupt_save = true;
    present_all_faces(&save_failed);
    interrupt_save = false;
    assert(imu_calibration_get_status().state == IMU_CALIBRATION_SAVE_FAILED);
    assert(writes == 1U);
    expect_calibration(0xD1U, &initial);
}

int main(void) {
    test_flash_recovery();
    test_combined_calibration();
    test_face_angle_limit();
    test_rejected_sessions();
    test_calibration_result_failures();
    return 0;
}
