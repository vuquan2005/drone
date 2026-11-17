#include "app.h"
#include "app_imu.h"
#include "app_log.h"
#include "app_motor.h"
#include "app_rate.h"
#include "app_safety.h"
#include "app_telemetry.h"
#include "bsp_board.h"
#include "bsp_time.h"
#include "main.h"
#include <stddef.h>

static app_imu_t imu;
static app_command_t command;
static app_rate_t rate;
static bool imu_available;
static bool pwm_ready;
static bool rate_failed;
static app_safety_t safety;
#if DRONE_RTT && DRONE_TELEMETRY
static uint32_t last_stats_ms;
static uint32_t last_rate_ms;
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
    pwm_ready = app_motor_init();
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
    last_rate_ms = last_stats_ms;
}
#else
static inline void init_telemetry_schedule(void) {}
#endif

static void process_command(const app_command_t *input) {
    const app_command_t previous = command;
    command = input != NULL && input->valid ? *input : (app_command_t){0};
    log_command_transition(&previous);
}

static bool imu_failed_for_rate(bsp_imu_result_t status) {
    return status != BSP_IMU_OK && status != BSP_IMU_NO_DATA;
}

bool app_configure_rate(const app_rate_config_t *config) {
    /* Reconfiguration revokes motor permission immediately. */
    safety = (app_safety_t){0};
    if (pwm_ready) {
        const norm_q15_t zero[APP_MOTOR_COUNT] = {0};
        if (!app_motor_set_all(zero)) {
            pwm_ready = false;
            app_motor_stop();
        }
    }
    return app_rate_configure(&rate, config);
}

bool app_get_rate_result(mixer_result_t *result) {
    if (result == NULL)
        return false;
    *result = rate.mixer;
    return rate.valid;
}

static void process_imu(void) {
    const bsp_imu_result_t status = app_imu_poll(&imu);
    log_imu_transition(status);
    if (imu_failed_for_rate(status)) {
        imu_available = false;
    }
    if (status == BSP_IMU_OK) {
        imu_available = true;
        APP_TELEMETRY_IMU(&imu.sample, imu.sample_count);
    }
}

static bool imu_healthy(void) {
    return imu_available &&
           (uint32_t)(bsp_time_now_us() - imu.sample.t_read_us) <= 20000U;
}

app_arm_state_t app_get_arm_state(void) {
    return safety.state;
}

static void process_rate(void) {
    rate_failed = false;
    /* Sensor freshness is independent of the controller's consumed timestamp.
     */
    if (!command.valid || !imu_healthy()) {
        app_rate_reset(&rate);
        return;
    }
    if (!rate.configured)
        return;
    if (rate.have_time && (uint32_t)(imu.sample.t_read_us - rate.previous_us) <
                              rate.config.period_us) {
        return;
    }
    /* Use the latest sample once; never replay missed controller iterations. */
    const bool had_time = rate.have_time;
    const bool calculated = app_rate_step(
        &rate, &command, imu.sample.gyro_radps, imu.sample.t_read_us);
    rate_failed = !calculated && (had_time || !rate.have_time);
}

static void process_motor(void) {
    const app_arm_state_t previous = safety.state;
    app_safety_update(&safety, &command, imu_healthy(),
                      rate.configured && !rate_failed, pwm_ready);
    if (previous != safety.state &&
        (previous == APP_ARMED || safety.state == APP_ARMED)) {
        app_rate_reset(&rate);
        APP_LOG_INFO("arm", "state=%u reason=%u", (unsigned)safety.state,
                     (unsigned)safety.reason);
    }
    norm_q15_t output[APP_MOTOR_COUNT] = {0};
    if (safety.state == APP_ARMED && command.throttle > APP_ARM_THROTTLE_MAX &&
        rate.valid) {
        /* Bench approximation: normalized thrust -> ESC command is linear.
         * PWM channels PB6..PB9 map to the confirmed M1..M4 wiring. */
        for (unsigned i = 0; i < APP_MOTOR_COUNT; ++i)
            output[i] = rate.mixer.motor[i];
    } else if (safety.state == APP_ARMED &&
               command.throttle <= APP_ARM_THROTTLE_MAX) {
        /* Mixer can raise collective at zero throttle; bypass it at idle. */
        app_rate_reset(&rate);
    }
    if (pwm_ready && !app_motor_set_all(output)) {
        pwm_ready = false;
        app_motor_stop();
        app_safety_update(&safety, &command, imu_healthy(), false, false);
        app_rate_reset(&rate);
        APP_LOG_ERROR("arm", "PWM failure: outputs disabled until app_init");
    }
}

#if DRONE_RTT && DRONE_TELEMETRY
static void process_periodic_telemetry(void) {
    const uint32_t now = HAL_GetTick();
    if ((uint32_t)(now - last_rate_ms) >= 50U) {
        last_rate_ms = now;
        if (rate.configured)
            APP_TELEMETRY_RATE(&rate);
    }
    if ((uint32_t)(now - last_stats_ms) >= 1000U) {
        last_stats_ms = now;
        APP_TELEMETRY_STATS(now, imu.sample_count, imu.read_error_count);
    }
}
#else
static inline void process_periodic_telemetry(void) {}
#endif

bool app_get_status(app_status_t *status) {
    if (status == NULL)
        return false;
    *status = (app_status_t){.imu_ready = imu_healthy(),
                             .motor_ready = pwm_ready,
                             .arm_state = safety.state};
    return true;
}

void app_init(void) {
    command = (app_command_t){0};
    rate = (app_rate_t){0};
    imu_available = false;
    pwm_ready = false;
    safety = (app_safety_t){0};
#if DRONE_RATE_CONTROL
    /* Bench gains only; motor permission is owned by the arm/failsafe state.
     */
    const app_rate_config_t rate_config = {
        .axis = {{.kp = 0.1f, .output_limit = 1.0f},
                 {.kp = 0.1f, .output_limit = 1.0f},
                 {.kp = 0.1f, .output_limit = 1.0f}},
        .max_rate_radps = {2.0f, 2.0f, 2.0f},
        .period_us = 2000U};
    const bool rate_ready = app_configure_rate(&rate_config);
    (void)rate_ready;
    APP_LOG_INFO("rate",
                 "configured=%u period_us=2000 P=0.1 I=0 D=0 max_radps=2",
                 (unsigned)rate_ready);
#endif
    init_motor();
    init_imu();
    init_telemetry_schedule();
}

void app_process(const app_command_t *input) {
    process_command(input);
    process_imu();
    process_rate();
    process_motor();
    process_periodic_telemetry();
}
