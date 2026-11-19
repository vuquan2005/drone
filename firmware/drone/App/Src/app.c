#include "app.h"
#include "app_imu.h"
#include "app_log.h"
#include "app_motor.h"
#include "app_rate.h"
#include "app_safety.h"
#include "app_telemetry.h"
#include "bsp_board.h"
#include "bsp_time.h"
#include "imu_calibration_store.h"
#include "mahony.h"
#include "main.h"
#include <stddef.h>

#define IMU_TIMEOUT_US 20000U
#define MAHONY_KP_PER_S 2.0f
#define MAHONY_KI_PER_S2 0.0f

static app_imu_t imu;
static mahony_t attitude;
static uint32_t attitude_time_us;
static bool attitude_valid;
static app_command_t command;
static app_rate_t rate;
static bool imu_available;
static bool pwm_ready;
static bool rate_failed;
static app_safety_t safety;
#if DRONE_RTT && DRONE_LOG_LEVEL >= APP_LOG_LEVEL_WARN
static bool last_arm_requested;
#endif
#if DRONE_RTT && DRONE_TELEMETRY
static uint32_t last_stats_ms;
static uint32_t last_control_ms;
static bool attitude_reported;
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
#if defined(DRONE_IMU_BMI160)
    /* Body FRD mean from the last 5 s of telemetry_20261119_023330. */
    imu.calibration = (app_imu_calibration_t){
        .gyro_bias_radps = {-0.001541997f, -0.004106014f, -0.005377476f}};
#endif
    log_imu_init();
    if (imu.status != BSP_IMU_OK) {
        return;
    }
    if (imu_calibration_load(imu.sensor.device.chip_id, &imu.calibration)) {
        APP_LOG_INFO("imu", "gyro bias loaded from flash");
        if (imu.calibration.accel_valid) {
            APP_LOG_INFO("imu", "accel calibration loaded from flash");
        }
    } else {
        APP_LOG_INFO("imu", "using default gyro bias");
    }
}

#if DRONE_RTT && DRONE_TELEMETRY
static void init_telemetry_schedule(void) {
    last_stats_ms = HAL_GetTick();
    last_control_ms = last_stats_ms;
    attitude_reported = false;
}
#else
static inline void init_telemetry_schedule(void) {}
#endif

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

static void update_attitude(const app_imu_sample_t *sample) {
    const float *accel = sample->accel_mps2;
    const float accel_sq =
        accel[0] * accel[0] + accel[1] * accel[1] + accel[2] * accel[2];
    /* Reject gravity feedback outside approximately 0.8 to 1.2 g. */
    const bool gravity_usable =
        accel_sq >= 7.85f * 7.85f && accel_sq <= 11.77f * 11.77f;
    const uint32_t elapsed_us = sample->t_read_us - attitude_time_us;
    if (!attitude_valid || elapsed_us > IMU_TIMEOUT_US) {
        attitude_valid = gravity_usable && mahony_init(&attitude, accel);
    } else if (elapsed_us == 0U) {
        return;
    } else {
        attitude_valid = mahony_update(
            &attitude, sample->gyro_radps, gravity_usable ? accel : NULL,
            elapsed_us * 1e-6f, MAHONY_KP_PER_S, MAHONY_KI_PER_S2);
    }
    attitude_time_us = sample->t_read_us;
}

static void process_imu(void) {
    const bsp_imu_result_t status = app_imu_poll(&imu);
    log_imu_transition(status);
    if (imu_failed_for_rate(status)) {
        imu_available = false;
        attitude_valid = false;
    }
    if (status == BSP_IMU_OK) {
        imu_available = true;
        update_attitude(&imu.sample);
        APP_TELEMETRY_IMU(&imu.sample, imu.sample_count);
    }
}

static bool imu_healthy(void) {
    return imu_available && (uint32_t)(bsp_time_now_us() -
                                       imu.sample.t_read_us) <= IMU_TIMEOUT_US;
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

#if DRONE_RTT && DRONE_LOG_LEVEL >= APP_LOG_LEVEL_WARN
static void log_arm_result(app_arm_state_t previous, app_safety_reason_t reason,
                           bool pwm_failed) {
    static const char *const reason_names[] = {
        [APP_SAFETY_RC] = "rc_invalid",
        [APP_SAFETY_IMU] = "imu_unhealthy",
        [APP_SAFETY_CONTROL] = "control_unhealthy",
        [APP_SAFETY_PWM] = "pwm_failure",
        [APP_SAFETY_CALIBRATION] = "esc_calibration_requested",
        [APP_SAFETY_ARM_OFF_REQUIRED] = "arm_off_required",
        [APP_SAFETY_THROTTLE_HIGH] = "throttle_high",
    };
    const bool new_request =
        command.valid && command.arm_requested && !last_arm_requested;
    /* A lost RC link does not establish that the switch went OFF. */
    if (command.valid)
        last_arm_requested = command.arm_requested;
    if (pwm_failed)
        return;
    if (previous == APP_ARMED && safety.state == APP_FAILSAFE) {
        APP_LOG_WARN("arm", "failsafe reason=%s", reason_names[reason]);
    } else if (previous == APP_ARMED && safety.state == APP_DISARMED) {
        APP_LOG_INFO("arm", "disarmed");
    } else if (previous != APP_ARMED && safety.state == APP_ARMED) {
        APP_LOG_INFO("arm", "armed");
    } else if (new_request && reason != APP_SAFETY_OK) {
        APP_LOG_WARN("arm", "rejected reason=%s", reason_names[reason]);
    }
}
#else
static inline void log_arm_result(app_arm_state_t previous,
                                  app_safety_reason_t reason, bool pwm_failed) {
    (void)previous;
    (void)reason;
    (void)pwm_failed;
}
#endif

static void process_motor(void) {
    const app_arm_state_t previous = safety.state;
    const app_safety_reason_t reason =
        app_safety_update(&safety, &command, imu_healthy(),
                          rate.configured && !rate_failed, pwm_ready);
    if (previous != safety.state &&
        (previous == APP_ARMED || safety.state == APP_ARMED)) {
        app_rate_reset(&rate);
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
    const bool pwm_failed = pwm_ready && !app_motor_set_all(output);
    if (pwm_failed) {
        pwm_ready = false;
        app_motor_stop();
        app_safety_update(&safety, &command, imu_healthy(), false, false);
        app_rate_reset(&rate);
        APP_LOG_ERROR(
            "arm",
            "failsafe reason=pwm_failure outputs disabled until app_init");
    }
    log_arm_result(previous, reason, pwm_failed);
}

#if DRONE_RTT && DRONE_TELEMETRY
static void process_periodic_telemetry(void) {
    const uint32_t now = HAL_GetTick();
    if ((uint32_t)(now - last_control_ms) >= 50U) {
        last_control_ms = now;
        if (rate.configured)
            APP_TELEMETRY_RATE(&rate);
        float q[4];
        const bool fresh = app_get_attitude(q);
        if (fresh) {
            (void)app_telemetry_write_attitude(q, attitude_time_us);
        }
        if (fresh != attitude_reported) {
            if (fresh)
                APP_LOG_INFO("attitude", "Mahony estimate ready");
            else
                APP_LOG_WARN("attitude", "Mahony estimate unavailable");
            attitude_reported = fresh;
        }
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

bool app_get_attitude(float body_to_ned[4]) {
    if (body_to_ned == NULL || !attitude_valid ||
        (uint32_t)(bsp_time_now_us() - attitude_time_us) > IMU_TIMEOUT_US) {
        return false;
    }
    for (size_t i = 0; i < 4; ++i) {
        body_to_ned[i] = attitude.q[i];
    }
    return true;
}

void app_init(void) {
    attitude_valid = false;
    command = (app_command_t){0};
    rate = (app_rate_t){0};
    imu_available = false;
    pwm_ready = false;
    safety = (app_safety_t){0};
#if DRONE_RTT && DRONE_LOG_LEVEL >= APP_LOG_LEVEL_WARN
    last_arm_requested = false;
#endif
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
    APP_LOG_DEBUG("rate",
                  "configured=%u period_us=2000 P=0.1 I=0 D=0 max_radps=2",
                  (unsigned)rate_ready);
#endif
    init_motor();
    init_imu();
    init_telemetry_schedule();
}

void app_process(const app_command_t *input) {
    command = input != NULL && input->valid ? *input : (app_command_t){0};
    process_imu();
    process_rate();
    process_motor();
    process_periodic_telemetry();
}
