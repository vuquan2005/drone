#include "app.h"
#include "app_imu.h"
#include "app_log.h"
#include "app_receiver.h"
#include "app_telemetry.h"
#include "bsp_board.h"
#include "bsp_pwm.h"
#include "bsp_rtt.h"
#include "main.h"

static app_imu_t imu;
static bool pwm_ready;
#if DRONE_RTT && DRONE_TELEMETRY
static uint32_t last_stats_ms;
static uint32_t last_receiver_ms;
#endif

#if DRONE_RTT && DRONE_LOG_LEVEL > APP_LOG_LEVEL_OFF
static bsp_imu_result_t last_imu_status;

static bool imu_failed(bsp_imu_result_t status) {
    return status != BSP_IMU_OK && status != BSP_IMU_NO_DATA;
}

static void log_imu_transition(bsp_imu_result_t status) {
    if (imu_failed(status) && status != last_imu_status) {
        APP_LOG_ERROR("imu", "read failed status=%d driver=%d", (int)status,
                      (int)imu.sensor.driver_error);
    } else if (!imu_failed(status) && imu_failed(last_imu_status)) {
        APP_LOG_INFO("imu", "read recovered");
    }
    last_imu_status = status;
}
#endif

void app_init(void) {
    bsp_rtt_init();
    app_telemetry_init();
    app_log_init(HAL_GetTick);
    pwm_ready = bsp_pwm_init();
    if (pwm_ready) {
        APP_LOG_INFO("pwm", "initialized");
    } else {
        APP_LOG_ERROR("pwm", "initialization failed");
    }
    app_imu_init(&imu, bsp_board_imu_config());
#if DRONE_RTT && DRONE_LOG_LEVEL > APP_LOG_LEVEL_OFF
    last_imu_status = imu.status;
    if (imu_failed(imu.status)) {
        APP_LOG_ERROR("imu", "initialization failed status=%d driver=%d",
                      (int)imu.status, (int)imu.sensor.driver_error);
    } else {
        APP_LOG_INFO("imu", "initialized id=0x%02x",
                     (unsigned)imu.sensor.device.chip_id);
    }
#endif
    app_receiver_init();
#if DRONE_RTT && DRONE_TELEMETRY
    last_stats_ms = HAL_GetTick();
    last_receiver_ms = last_stats_ms;
#endif
}

void app_process(void) {
    app_receiver_poll();
    bsp_imu_result_t status = app_imu_poll(&imu);
#if DRONE_RTT && DRONE_LOG_LEVEL > APP_LOG_LEVEL_OFF
    log_imu_transition(status);
#endif
    if (status == BSP_IMU_OK) {
        APP_TELEMETRY_IMU(&imu.sample, imu.sample_count);
    }
#if DRONE_RTT && DRONE_TELEMETRY
    const uint32_t now = HAL_GetTick();
    if ((uint32_t)(now - last_receiver_ms) >= 50U) {
        last_receiver_ms = now;
        app_receiver_sample_t sample;
        if (app_receiver_get_sample(&sample)) {
            APP_TELEMETRY_RECEIVER(&sample, HAL_GetTick());
        }
    }
    if ((uint32_t)(now - last_stats_ms) >= 1000U) {
        last_stats_ms = now;
        APP_TELEMETRY_STATS(now, imu.sample_count, imu.read_error_count);
    }
#endif
}
