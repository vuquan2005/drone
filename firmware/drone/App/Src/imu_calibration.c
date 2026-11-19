#include "imu_calibration.h"
#include "accel_calibration.h"
#include "app_log.h"
#include "app_safety.h"
#include "app_telemetry.h"
#include "bsp_board.h"
#include "gyro_bias.h"
#include "imu_calibration_store.h"
#include "main.h"

#define IMU_CAL_SETTLE_MS 1000U
#define GYRO_CAL_TIMEOUT_MS 20000U
#define ACCEL_CAL_TIMEOUT_MS 120000U
#define GYRO_CAL_WINDOW_US 3000000U
#define IMU_CAL_SAMPLE_GAP_US 20000U
#define MIN_SAMPLES(window_us)                                                 \
    (((window_us) / 1000000U) * BSP_IMU_ODR_HZ * 95U / 100U)

static app_imu_t imu;
static app_imu_calibration_t calibration;
static gyro_bias_window_t gyro_window;
static accel_calibration_t accel_session;
static imu_calibration_state_t state;
static uint32_t phase_start_ms;
static uint32_t first_us;
static uint32_t last_us;

void imu_calibration_init(void) {
    gyro_window = (gyro_bias_window_t){0};
    accel_session = (accel_calibration_t){0};
    calibration = (app_imu_calibration_t){0};
    const bsp_imu_result_t status = app_imu_init(&imu, bsp_board_imu_config());
    state = status == BSP_IMU_OK ? IMU_CALIBRATION_GYRO
                                 : IMU_CALIBRATION_INIT_FAILED;
    phase_start_ms = HAL_GetTick();
    if (state == IMU_CALIBRATION_GYRO) {
        APP_LOG_INFO("imu_calib", "gyro: keep the frame stationary");
    } else {
        APP_LOG_ERROR("imu_calib", "IMU init failed status=%d driver=%d",
                      (int)status, (int)imu.sensor.driver_error);
    }
}

static void process_gyro(const app_imu_sample_t *sample) {
    const uint32_t now_us = sample->t_read_us;
    if (gyro_window.samples == 0U || now_us - last_us > IMU_CAL_SAMPLE_GAP_US) {
        gyro_window = (gyro_bias_window_t){0};
        first_us = now_us;
    }
    last_us = now_us;
    if (!gyro_bias_push(&gyro_window, sample->accel_mps2, sample->gyro_radps)) {
        gyro_window = (gyro_bias_window_t){0};
        return;
    }
    if (now_us - first_us < GYRO_CAL_WINDOW_US) {
        return;
    }
    if (gyro_window.samples < MIN_SAMPLES(GYRO_CAL_WINDOW_US) ||
        !gyro_bias_result(&gyro_window, calibration.gyro_bias_radps)) {
        gyro_window = (gyro_bias_window_t){0};
        APP_LOG_WARN("imu_calib", "gyro window rejected, collecting again");
        return;
    }
    state = IMU_CALIBRATION_ACCEL;
    phase_start_ms = HAL_GetTick();
    APP_LOG_INFO("imu_calib", "gyro bias urad/s=%ld,%ld,%ld samples=%lu",
                 (long)(calibration.gyro_bias_radps[0] * 1e6f),
                 (long)(calibration.gyro_bias_radps[1] * 1e6f),
                 (long)(calibration.gyro_bias_radps[2] * 1e6f),
                 (unsigned long)gyro_window.samples);
    APP_LOG_INFO("imu_calib",
                 "accel: expose each face; remaining X+ X- Y+ Y- Z+ Z-");
}

#if DRONE_RTT && DRONE_LOG_LEVEL >= APP_LOG_LEVEL_INFO
static void log_accel_face(accel_cal_face_t face, unsigned captured_faces) {
    static const char names[ACCEL_CAL_NO_FACE][3] = {"X+", "X-", "Y+",
                                                     "Y-", "Z+", "Z-"};
    char remaining[3U * ACCEL_CAL_NO_FACE + 1U];
    unsigned length = 0U;
    for (unsigned i = 0U; i < ACCEL_CAL_NO_FACE; ++i) {
        if (!(captured_faces & (1U << i))) {
            remaining[length++] = ' ';
            remaining[length++] = names[i][0];
            remaining[length++] = names[i][1];
        }
    }
    remaining[length] = '\0';
    APP_LOG_INFO("imu_calib", "accel: captured %s; remaining%s", names[face],
                 length == 0U ? " none" : remaining);
}
#else
static inline void log_accel_face(accel_cal_face_t face,
                                  unsigned captured_faces) {
    (void)face;
    (void)captured_faces;
}
#endif

static void process_accel(const app_imu_sample_t *sample) {
    float gyro_radps[3];
    for (unsigned axis = 0U; axis < 3U; ++axis) {
        gyro_radps[axis] =
            sample->gyro_radps[axis] - calibration.gyro_bias_radps[axis];
    }
    const accel_cal_face_t face = accel_calibration_push(
        &accel_session, sample->accel_mps2, gyro_radps, sample->t_read_us,
        MIN_SAMPLES(ACCEL_CAL_FACE_WINDOW_US));
    if (face == ACCEL_CAL_NO_FACE) {
        return;
    }
    log_accel_face(face, accel_session.captured_faces);
    if (accel_session.captured_faces != ACCEL_CAL_ALL_FACES) {
        return;
    }
    calibration.accel_valid = accel_calibration_result(
        &accel_session, calibration.accel_bias_mps2, calibration.accel_scale);
    if (!calibration.accel_valid) {
        state = IMU_CALIBRATION_INVALID_ACCEL;
        APP_LOG_ERROR("imu_calib",
                      "accel coefficients out of range; reset to retry");
    } else if (imu_calibration_save(imu.sensor.device.chip_id, &calibration)) {
        state = IMU_CALIBRATION_DONE;
        APP_LOG_INFO("imu_calib",
                     "saved gyro and accel calibration; reset to use");
    } else {
        state = IMU_CALIBRATION_SAVE_FAILED;
        APP_LOG_ERROR("imu_calib", "flash save failed");
    }
}

void imu_calibration_process(const app_command_t *command) {
    if (state != IMU_CALIBRATION_GYRO && state != IMU_CALIBRATION_ACCEL) {
        return;
    }
    const uint32_t elapsed_ms = HAL_GetTick() - phase_start_ms;
    /* Give the receiver time to recover after blocking IMU initialization. */
    if (state == IMU_CALIBRATION_GYRO && elapsed_ms < IMU_CAL_SETTLE_MS) {
        return;
    }
    const bool rc_valid = command != NULL && command->valid;
    if (!rc_valid || !command->imu_calibration_requested ||
        command->arm_requested || command->throttle < 0 ||
        command->throttle > APP_ARM_THROTTLE_MAX) {
        if (!rc_valid) {
            state = IMU_CALIBRATION_RC_LOST;
            APP_LOG_WARN("imu_calib", "cancelled: RC unavailable");
        } else {
            state = IMU_CALIBRATION_CANCELLED;
            APP_LOG_WARN(
                "imu_calib", "cancelled: requested=%u arm=%u throttle_q15=%ld",
                (unsigned)command->imu_calibration_requested,
                (unsigned)command->arm_requested, (long)command->throttle);
        }
        return;
    }
    const uint32_t timeout_ms = state == IMU_CALIBRATION_GYRO
                                    ? GYRO_CAL_TIMEOUT_MS
                                    : ACCEL_CAL_TIMEOUT_MS;
    if (elapsed_ms >= timeout_ms) {
        APP_LOG_ERROR("imu_calib", "%s calibration timed out",
                      state == IMU_CALIBRATION_GYRO ? "gyro" : "accel");
        state = IMU_CALIBRATION_TIMEOUT;
        return;
    }
    const bsp_imu_result_t status = app_imu_poll(&imu);
    if (status == BSP_IMU_NO_DATA) {
        return;
    }
    if (status != BSP_IMU_OK) {
        state = IMU_CALIBRATION_READ_FAILED;
        APP_LOG_ERROR("imu_calib", "IMU read failed status=%d driver=%d",
                      (int)status, (int)imu.sensor.driver_error);
        return;
    }

    APP_TELEMETRY_IMU(&imu.sample, imu.sample_count);
    if (state == IMU_CALIBRATION_GYRO) {
        process_gyro(&imu.sample);
    } else {
        process_accel(&imu.sample);
    }
}

imu_calibration_status_t imu_calibration_get_status(void) {
    return (imu_calibration_status_t){
        .state = state, .captured_faces = accel_session.captured_faces};
}
