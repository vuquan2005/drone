#include "imu_calibration.h"
#include "app_log.h"
#include "app_safety.h"
#include "bsp_board.h"
#include "gyro_bias.h"
#include "imu_calibration_store.h"
#include "main.h"

#define IMU_CAL_SETTLE_MS 1000U
#define IMU_CAL_TIMEOUT_MS 20000U
#define IMU_CAL_WINDOW_US 3000000U
#define IMU_CAL_SAMPLE_GAP_US 20000U
#define IMU_CAL_MIN_SAMPLES                                                    \
    ((IMU_CAL_WINDOW_US / 1000000U) * BSP_IMU_ODR_HZ * 95U / 100U)

static app_imu_t imu;
static gyro_bias_window_t window;
static bool active;
static uint32_t start_ms;
static uint32_t first_us;
static uint32_t last_us;

void imu_calibration_init(void) {
    window = (gyro_bias_window_t){0};
    const bsp_imu_result_t status = app_imu_init(&imu, bsp_board_imu_config());
    active = status == BSP_IMU_OK;
    start_ms = HAL_GetTick();
    if (active) {
        APP_LOG_INFO("imu_calib", "keep the frame stationary");
    } else {
        APP_LOG_ERROR("imu_calib", "IMU init failed status=%d driver=%d",
                      (int)status, (int)imu.sensor.driver_error);
    }
}

void imu_calibration_process(const app_command_t *command) {
    if (!active) {
        return;
    }
    const uint32_t elapsed_ms = HAL_GetTick() - start_ms;
    /* Give the receiver time to recover after blocking IMU initialization. */
    if (elapsed_ms < IMU_CAL_SETTLE_MS) {
        return;
    }
    const bool rc_valid = command != NULL && command->valid;
    if (!rc_valid || !command->imu_calibration_requested ||
        command->arm_requested || command->throttle < 0 ||
        command->throttle > APP_ARM_THROTTLE_MAX) {
        active = false;
        if (!rc_valid) {
            APP_LOG_WARN("imu_calib", "cancelled: RC unavailable");
        } else {
            APP_LOG_WARN(
                "imu_calib", "cancelled: requested=%u arm=%u throttle_q15=%ld",
                (unsigned)command->imu_calibration_requested,
                (unsigned)command->arm_requested, (long)command->throttle);
        }
        return;
    }
    if (elapsed_ms >= IMU_CAL_TIMEOUT_MS) {
        active = false;
        APP_LOG_ERROR("imu_calib", "timed out waiting for stationary samples");
        return;
    }
    const bsp_imu_result_t status = app_imu_poll(&imu);
    if (status == BSP_IMU_NO_DATA) {
        return;
    }
    if (status != BSP_IMU_OK) {
        active = false;
        APP_LOG_ERROR("imu_calib", "IMU read failed status=%d driver=%d",
                      (int)status, (int)imu.sensor.driver_error);
        return;
    }

    const uint32_t now_us = imu.sample.t_read_us;
    if (window.samples == 0U || now_us - last_us > IMU_CAL_SAMPLE_GAP_US) {
        window = (gyro_bias_window_t){0};
        first_us = now_us;
    }
    last_us = now_us;
    if (!gyro_bias_push(&window, imu.sample.accel_mps2,
                        imu.sample.gyro_radps)) {
        window = (gyro_bias_window_t){0};
        return;
    }
    if (now_us - first_us < IMU_CAL_WINDOW_US) {
        return;
    }

    app_imu_calibration_t calibration;
    if (window.samples < IMU_CAL_MIN_SAMPLES ||
        !gyro_bias_result(&window, calibration.gyro_bias_radps)) {
        window = (gyro_bias_window_t){0};
        APP_LOG_WARN("imu_calib", "window rejected, collecting again");
        return;
    }
    active = false;
    if (imu_calibration_save(imu.sensor.device.chip_id, &calibration)) {
        APP_LOG_INFO("imu_calib",
                     "saved bias urad/s=%ld,%ld,%ld samples=%lu; reset to use",
                     (long)(calibration.gyro_bias_radps[0] * 1e6f),
                     (long)(calibration.gyro_bias_radps[1] * 1e6f),
                     (long)(calibration.gyro_bias_radps[2] * 1e6f),
                     (unsigned long)window.samples);
    } else {
        APP_LOG_ERROR("imu_calib", "flash save failed");
    }
}
