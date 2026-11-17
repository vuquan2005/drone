#include "app.h"
#include "app_imu.h"
#include "app_log.h"
#include "app_motor.h"
#include "app_receiver.h"
#include "app_telemetry.h"
#include "bsp_board.h"
#include "bsp_rtt.h"
#include "main.h"
#include <stddef.h>

static app_imu_t imu;
static app_command_t command;
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

static void log_imu_init(void) {
    last_imu_status = imu.status;
    if (imu_failed(imu.status)) {
        APP_LOG_ERROR("imu", "initialization failed status=%d driver=%d",
                      (int)imu.status, (int)imu.sensor.driver_error);
    } else {
        APP_LOG_INFO("imu", "initialized id=0x%02x",
                     (unsigned)imu.sensor.device.chip_id);
    }
}
#else
static inline void log_imu_transition(bsp_imu_result_t status) {
    (void)status;
}

static inline void log_imu_init(void) {}
#endif

#if DRONE_RTT && DRONE_LOG_LEVEL >= APP_LOG_LEVEL_INFO
static void log_command_transition(const app_command_t *previous) {
    if (!command.valid) {
        if (previous->valid) {
            APP_LOG_INFO("command", "valid=0");
        }
        return;
    }
    if (previous->valid && previous->arm_requested == command.arm_requested &&
        previous->mode == command.mode &&
        previous->esc_calibration_requested ==
            command.esc_calibration_requested &&
        previous->telemetry_mode == command.telemetry_mode) {
        return;
    }
    static const char *const mode_names[] = {"low", "middle", "high"};
    static const char *const telemetry_names[] = {"off", "stream", "record"};
    APP_LOG_INFO("command",
                 "valid=1 arm_requested=%u mode=%s "
                 "esc_calibration_requested=%u telemetry=%s",
                 (unsigned)command.arm_requested, mode_names[command.mode],
                 (unsigned)command.esc_calibration_requested,
                 telemetry_names[command.telemetry_mode]);
}
#else
static inline void log_command_transition(const app_command_t *previous) {
    (void)previous;
}
#endif

static void init_motor(void) {
    const bool pwm_ready = app_motor_init();
    if (pwm_ready) {
        APP_LOG_INFO("pwm", "initialized");
    } else {
        APP_LOG_ERROR("pwm", "initialization failed");
    }
}

static void init_imu(void) {
    app_imu_init(&imu, bsp_board_imu_config());
    log_imu_init();
}

#if DRONE_RTT && DRONE_TELEMETRY
static void init_telemetry_schedule(void) {
    last_stats_ms = HAL_GetTick();
    last_receiver_ms = last_stats_ms;
}
#else
static inline void init_telemetry_schedule(void) {}
#endif

static bool process_receiver(app_receiver_sample_t *sample) {
    const app_command_t previous = command;
    const bool available = app_receiver_get_sample(sample);
    (void)app_command_decode(available ? sample : NULL, &command);
    log_command_transition(&previous);
    return available;
}

static void process_imu(void) {
    const bsp_imu_result_t status = app_imu_poll(&imu);
    log_imu_transition(status);
    if (status == BSP_IMU_OK) {
        APP_TELEMETRY_IMU(&imu.sample, imu.sample_count);
    }
}

#if DRONE_RTT && DRONE_TELEMETRY
static void process_periodic_telemetry(const app_receiver_sample_t *sample,
                                       bool receiver_available) {
    const uint32_t now = HAL_GetTick();
    if ((uint32_t)(now - last_receiver_ms) >= 50U) {
        last_receiver_ms = now;
        if (receiver_available) {
            APP_TELEMETRY_RECEIVER(sample, HAL_GetTick());
        }
    }
    if ((uint32_t)(now - last_stats_ms) >= 1000U) {
        last_stats_ms = now;
        APP_TELEMETRY_STATS(now, imu.sample_count, imu.read_error_count);
    }
}
#else
static inline void
process_periodic_telemetry(const app_receiver_sample_t *sample,
                           bool receiver_available) {
    (void)sample;
    (void)receiver_available;
}
#endif

bool app_get_command(app_command_t *sample) {
    if (sample == NULL) {
        return false;
    }
    *sample = command;
    return true;
}

void app_init(void) {
    command = (app_command_t){0};
    bsp_rtt_init();
    app_telemetry_init();
    app_log_init(HAL_GetTick);
    init_motor();
    init_imu();
    app_receiver_init();
    init_telemetry_schedule();
}

void app_process(void) {
    app_receiver_sample_t receiver_sample = {0};
    const bool receiver_available = process_receiver(&receiver_sample);
    process_imu();
    process_periodic_telemetry(&receiver_sample, receiver_available);
}
