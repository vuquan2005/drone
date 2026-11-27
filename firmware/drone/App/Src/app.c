#include "app.h"
#include "app_imu.h"
#include "app_log.h"
#include "app_motor.h"
#include "app_rate.h"
#include "app_safety.h"
#include "app_telemetry.h"
#include "attitude_controller.h"
#include "bsp_board.h"
#include "bsp_time.h"
#include "imu_calibration_store.h"
#include "imu_lowpass.h"
#include "mahony.h"
#include "main.h"
#include <stddef.h>

#define IMU_TIMEOUT_US 20000U
#define RATE_GYRO_LPF_HZ 80.0f
#define MAHONY_ACCEL_LPF_HZ 30.0f
#define MAHONY_HZ 200U
#define ATTITUDE_CONTROL_HZ 100U
#define MAHONY_KP_PER_S 2.0f
#define MAHONY_KI_PER_S2 0.0f
#define ATTITUDE_KP_PER_S 4.14f
#define ATTITUDE_MAX_ANGLE_RAD (25.0f * 0.01745329252f)
#define GRAVITY_MIN_MPS2 (0.9f * 9.80665f)
#define GRAVITY_MAX_MPS2 (1.1f * 9.80665f)
#define RATE_INTEGRAL_LIMIT 0.10f
#define RATE_D_FILTER_TAU_S 0.008f
#define RATE_OUTPUT_LIMIT 1.0f
#define RATE_ROLL_MAX_RADPS 2.0f
#define RATE_PITCH_MAX_RADPS 2.0f
#define RATE_YAW_MAX_RADPS 2.0f
#define RATE_PERIOD_US 2000U
#define RATE_TRIM_FRACTION 0.25f

_Static_assert(BSP_IMU_ODR_HZ >= MAHONY_HZ && BSP_IMU_ODR_HZ % MAHONY_HZ == 0U,
               "Mahony updates require an integer IMU decimation factor");
_Static_assert(MAHONY_HZ >= ATTITUDE_CONTROL_HZ &&
                   MAHONY_HZ % ATTITUDE_CONTROL_HZ == 0U,
               "Attitude control requires an integer Mahony decimation factor");

static app_imu_t imu;
static imu_lowpass_t imu_lowpass;
static uint32_t imu_lowpass_sample_us;
static struct {
    mahony_t filter;
    float gyro_delta_rad[3];
    uint32_t sample_us;
    uint32_t estimate_us;
    uint32_t updates;
    unsigned intervals;
    bool valid;
} attitude;
static struct {
    attitude_control_result_t result;
    uint32_t attitude_updates;
    bool have_update;
    bool valid;
} tilt_target;
static app_command_t command;
static app_rate_t rate;
static app_rate_config_t base_rate_config;
static bool imu_initialized;
static bool imu_calibration_from_flash;
static bool imu_available;
static bool pwm_ready;
static bool rate_failed;
static app_safety_t safety;
#if APP_LOG_ENABLED(APP_LOG_LEVEL_WARN)
static bool last_arm_requested;
static bool attitude_reported;
#endif
#if APP_TELEMETRY_ENABLED || APP_LOG_ENABLED(APP_LOG_LEVEL_WARN)
static uint32_t last_control_ms;
#endif
#if APP_TELEMETRY_ENABLED
static uint32_t last_stats_ms;
static app_attitude_control_sample_t attitude_control_sample;
static bool attitude_control_pending;
#endif

static bool imu_failed(bsp_imu_result_t status) {
    return status != BSP_IMU_OK && status != BSP_IMU_NO_DATA;
}

#if APP_LOG_ENABLED(APP_LOG_LEVEL_ERROR)
static bsp_imu_result_t last_imu_status;

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
    imu_calibration_from_flash = false;
    imu_initialized = app_imu_init(&imu, bsp_board_imu_config()) == BSP_IMU_OK;
#if defined(DRONE_IMU_BMI160)
    /* Body FRD mean from the last 5 s of telemetry_20261119_023330. */
    imu.calibration = (app_imu_calibration_t){
        .gyro_bias_radps = {-0.001541997f, -0.004106014f, -0.005377476f}};
#endif
    log_imu_init();
    if (imu.status != BSP_IMU_OK) {
        return;
    }
    imu_calibration_from_flash =
        imu_calibration_load(imu.sensor.device.chip_id, &imu.calibration);
    APP_LOG_INFO("imu", "calibration_applied source=%s accel_enabled=%u",
                 imu_calibration_from_flash ? "flash" : "default",
                 (unsigned)imu.calibration.accel_valid);
}

app_config_t app_get_config(void) {
    return (app_config_t){
        .mode = APP_CONFIG_FLIGHT,
        .imu = &imu,
        .calibration_from_flash = imu_calibration_from_flash,
        .rate = rate.configured ? &rate.config : NULL,
        .imu_timeout_us = IMU_TIMEOUT_US,
        .mahony_hz = MAHONY_HZ,
        .attitude_hz = ATTITUDE_CONTROL_HZ,
        .mahony_kp_per_s = MAHONY_KP_PER_S,
        .mahony_ki_per_s2 = MAHONY_KI_PER_S2,
        .attitude_kp_per_s = ATTITUDE_KP_PER_S,
        .attitude_max_rad = ATTITUDE_MAX_ANGLE_RAD,
        .gravity_min_mps2 = GRAVITY_MIN_MPS2,
        .gravity_max_mps2 = GRAVITY_MAX_MPS2,
    };
}

bool app_configure_rate(const app_rate_config_t *config) {
    /* Reconfiguration revokes motor permission immediately. */
    safety = (app_safety_t){0};
    tilt_target.have_update = false;
    if (pwm_ready) {
        const norm_q15_t zero[APP_MOTOR_COUNT] = {0};
        if (!app_motor_set_all(zero)) {
            pwm_ready = false;
            app_motor_stop();
        }
    }
    if (!app_rate_configure(&rate, config))
        return false;
    base_rate_config = rate.config;
    return true;
}

bool app_get_rate_result(mixer_result_t *result) {
    if (result == NULL)
        return false;
    *result = rate.mixer;
    return rate.valid;
}

static void update_attitude(const app_imu_sample_t *sample) {
    const uint32_t elapsed_us = sample->t_read_us - attitude.estimate_us;
    const bool initialize = !attitude.valid || elapsed_us > IMU_TIMEOUT_US;
    if (!initialize) {
        const uint32_t interval_us = sample->t_read_us - attitude.sample_us;
        if (interval_us == 0U)
            return;
        /* Retain the rotation from each sample interval when decimating. */
        const float interval_s = interval_us * 1e-6f;
        for (unsigned axis = 0; axis < 3U; ++axis) {
            attitude.gyro_delta_rad[axis] +=
                sample->gyro_radps[axis] * interval_s;
        }
        attitude.sample_us = sample->t_read_us;
        if (++attitude.intervals < BSP_IMU_ODR_HZ / MAHONY_HZ)
            return;
    }

    const float *accel = imu_lowpass.accel_mps2;
    const float accel_sq =
        accel[0] * accel[0] + accel[1] * accel[1] + accel[2] * accel[2];
    /* Reject gravity feedback outside approximately 0.9 to 1.1 g. */
    const bool gravity_usable =
        accel_sq >= GRAVITY_MIN_MPS2 * GRAVITY_MIN_MPS2 &&
        accel_sq <= GRAVITY_MAX_MPS2 * GRAVITY_MAX_MPS2;
    if (initialize) {
        tilt_target.have_update = false;
        attitude.valid = gravity_usable && mahony_init(&attitude.filter, accel);
    } else {
        const float dt_s = elapsed_us * 1e-6f;
        const float inverse_dt = 1.0f / dt_s;
        float mean_gyro_radps[3];
        for (unsigned axis = 0; axis < 3U; ++axis) {
            mean_gyro_radps[axis] = attitude.gyro_delta_rad[axis] * inverse_dt;
        }
        attitude.valid = mahony_update(&attitude.filter, mean_gyro_radps,
                                       gravity_usable ? accel : NULL, dt_s,
                                       MAHONY_KP_PER_S, MAHONY_KI_PER_S2);
    }
    attitude.intervals = 0U;
    for (unsigned axis = 0; axis < 3U; ++axis) {
        attitude.gyro_delta_rad[axis] = 0.0f;
    }
    attitude.sample_us = sample->t_read_us;
    attitude.estimate_us = sample->t_read_us;
    if (attitude.valid)
        attitude.updates++;
}

static void process_imu(void) {
    const bsp_imu_result_t status = app_imu_poll(&imu);
    log_imu_transition(status);
    if (imu_failed(status)) {
        imu_available = false;
        attitude.valid = false;
        imu_lowpass.initialized = false;
    }
    if (status == BSP_IMU_OK) {
        const uint32_t interval_us =
            imu.sample.t_read_us - imu_lowpass_sample_us;
        if (interval_us > IMU_TIMEOUT_US)
            imu_lowpass.initialized = false;
        if (!imu_lowpass.initialized || interval_us != 0U) {
            imu_lowpass_update(&imu_lowpass, imu.sample.gyro_radps,
                               imu.sample.accel_mps2, interval_us * 1e-6f);
            imu_lowpass_sample_us = imu.sample.t_read_us;
        }
        imu_available = true;
        update_attitude(&imu.sample);
    }
}

static bool imu_healthy(void) {
    return imu_available && (uint32_t)(bsp_time_now_us() -
                                       imu.sample.t_read_us) <= IMU_TIMEOUT_US;
}

static bool attitude_is_fresh(void) {
    return attitude.valid && (uint32_t)(bsp_time_now_us() -
                                        attitude.estimate_us) <= IMU_TIMEOUT_US;
}

static bool process_tilt_target(void) {
    if (!attitude_is_fresh()) {
        tilt_target.have_update = false;
        return false;
    }
    if (!tilt_target.have_update ||
        (uint32_t)(attitude.updates - tilt_target.attitude_updates) >=
            MAHONY_HZ / ATTITUDE_CONTROL_HZ) {
        const float angle_per_q15 = ATTITUDE_MAX_ANGLE_RAD / NORM_Q15_ONE;
        const float roll_rad = command.roll * angle_per_q15;
        const float pitch_rad = command.pitch * angle_per_q15;
#if APP_TELEMETRY_ENABLED
        const uint32_t t_control_us = bsp_time_now_us();
#endif
        tilt_target.valid =
            attitude_controller_step(attitude.filter.q, roll_rad, pitch_rad,
                                     ATTITUDE_KP_PER_S, &tilt_target.result);
        tilt_target.attitude_updates = attitude.updates;
        tilt_target.have_update = true;
#if APP_TELEMETRY_ENABLED
        const float *q = attitude.filter.q;
        const attitude_control_result_t *result = &tilt_target.result;
        attitude_control_sample = (app_attitude_control_sample_t){
            .t_control_us = t_control_us,
            .t_estimate_us = attitude.estimate_us,
            .body_to_ned = {q[0], q[1], q[2], q[3]},
            .target_rad = {roll_rad, pitch_rad},
            .tilt_error = {result->tilt_error[0], result->tilt_error[1]},
            .rate_radps = {result->rate_radps[0], result->rate_radps[1]},
            .valid = tilt_target.valid};
        attitude_control_pending = true;
#endif
    }
    return tilt_target.valid;
}

app_arm_state_t app_get_arm_state(void) {
    return safety.state;
}

static void process_rate(void) {
    rate_failed = false;
    /* Sensor freshness is independent of the controller's consumed timestamp.
     */
    if (!command.valid || !imu_healthy()) {
        tilt_target.have_update = false;
        app_rate_reset(&rate);
        return;
    }
    if (!rate.configured)
        return;
    const bool use_attitude = command.mode != APP_COMMAND_SWITCH_LOW;
    if (use_attitude && !process_tilt_target()) {
        rate_failed = true;
        app_rate_reset(&rate);
        return;
    }
    if (!imu.sample_valid)
        return;
    if (rate.have_time && (uint32_t)(imu.sample.t_read_us - rate.previous_us) <
                              rate.config.period_us) {
        return;
    }
    /* Use the latest sample once; never replay missed controller iterations. */
    const bool had_time = rate.have_time;
    const bool calculated = app_rate_step_with_tilt(
        &rate, &command, use_attitude ? tilt_target.result.rate_radps : NULL,
        imu_lowpass.gyro_radps, imu.sample.t_read_us);
    rate_failed = !calculated && (had_time || !rate.have_time);
}

#if APP_LOG_ENABLED(APP_LOG_LEVEL_WARN)
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
    } else if (reason == APP_SAFETY_IDLE_TIMEOUT) {
        APP_LOG_WARN("arm", "disarmed reason=idle_timeout");
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

static float rate_trim_scale(norm_q15_t trim) {
    return 1.0f + RATE_TRIM_FRACTION * trim / NORM_Q15_ONE;
}

static void apply_rate_trim(void) {
    const float roll_pitch_scale = rate_trim_scale(command.analog_aux[0]);
    const float yaw_scale = rate_trim_scale(command.analog_aux[1]);
    const float rate_scale = rate_trim_scale(command.analog_aux[2]);
    const float kp_scale[RATE_AXIS_COUNT] = {roll_pitch_scale, roll_pitch_scale,
                                             yaw_scale};
    for (unsigned i = 0; i < RATE_AXIS_COUNT; ++i) {
        rate.config.axis[i].kp = base_rate_config.axis[i].kp * kp_scale[i];
        rate.config.max_rate_radps[i] =
            base_rate_config.max_rate_radps[i] * rate_scale;
    }
}

static void process_motor(void) {
    const app_arm_state_t previous = safety.state;
    const uint32_t now_ms = HAL_GetTick();
    const app_safety_reason_t reason =
        app_safety_update(&safety, &command, imu_healthy(),
                          rate.configured && !rate_failed, pwm_ready, now_ms);
    if (previous != safety.state &&
        (previous == APP_ARMED || safety.state == APP_ARMED)) {
        if (safety.state == APP_ARMED)
            apply_rate_trim();
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
        app_safety_update(&safety, &command, imu_healthy(), false, false,
                          now_ms);
        app_rate_reset(&rate);
        APP_LOG_ERROR(
            "arm",
            "failsafe reason=pwm_failure outputs disabled until app_init");
    }
    log_arm_result(previous, reason, pwm_failed);
}

#if APP_TELEMETRY_ENABLED || APP_LOG_ENABLED(APP_LOG_LEVEL_WARN)
static void process_diagnostics(void) {
#if APP_TELEMETRY_ENABLED
    if (attitude_control_pending) {
        /* Encode after the motor update to keep RTT off the output path. */
        attitude_control_pending = false;
        (void)app_telemetry_write_attitude_control(&attitude_control_sample);
    }
#endif
    const uint32_t now = HAL_GetTick();
    if ((uint32_t)(now - last_control_ms) >= 50U) {
        last_control_ms = now;
#if APP_TELEMETRY_ENABLED
        if (rate.configured)
            (void)app_telemetry_write_rate(&rate);
#endif
        const bool fresh = attitude_is_fresh();
#if APP_TELEMETRY_ENABLED
        if (fresh) {
            (void)app_telemetry_write_attitude(attitude.filter.q,
                                               attitude.estimate_us);
        }
#endif
#if APP_LOG_ENABLED(APP_LOG_LEVEL_WARN)
        if (fresh != attitude_reported) {
            if (fresh)
                APP_LOG_INFO("attitude",
                             "estimate_state filter=mahony state=ready");
            else
                APP_LOG_WARN("attitude",
                             "estimate_state filter=mahony state=unavailable");
            attitude_reported = fresh;
        }
#endif
    }
#if APP_TELEMETRY_ENABLED
    if ((uint32_t)(now - last_stats_ms) >= 1000U) {
        last_stats_ms = now;
        (void)app_telemetry_write_stats(now, imu.sample_count,
                                        imu.read_error_count);
    }
#endif
}
#else
static inline void process_diagnostics(void) {}
#endif

bool app_get_status(app_status_t *status) {
    if (status == NULL)
        return false;
    *status = (app_status_t){.imu_initialized = imu_initialized,
                             .imu_ready = imu_healthy(),
                             .motor_ready = pwm_ready,
                             .arm_state = safety.state,
                             .safety_reason = safety.reason};
    return true;
}

bool app_get_attitude(float body_to_ned[4]) {
    if (body_to_ned == NULL || !attitude_is_fresh()) {
        return false;
    }
    for (size_t i = 0; i < 4; ++i) {
        body_to_ned[i] = attitude.filter.q[i];
    }
    return true;
}

void app_init(void) {
    imu_lowpass_init(&imu_lowpass, BSP_IMU_ODR_HZ, RATE_GYRO_LPF_HZ,
                     MAHONY_ACCEL_LPF_HZ);
    imu_lowpass_sample_us = 0U;
    attitude.valid = false;
    attitude.updates = 0U;
    tilt_target.have_update = false;
    command = (app_command_t){0};
    rate = (app_rate_t){0};
    imu_available = false;
    pwm_ready = false;
    safety = (app_safety_t){0};
#if APP_LOG_ENABLED(APP_LOG_LEVEL_WARN)
    last_arm_requested = false;
    attitude_reported = false;
#endif
#if DRONE_RATE_CONTROL
    /* Bench gains only; motor permission is owned by the arm/failsafe state.
     */
    const app_rate_config_t rate_config = {
        .axis = {{.kp = 0.123f,
                  .ki = 0.485f,
                  .kd = 0.00250f,
                  .integral_limit = RATE_INTEGRAL_LIMIT,
                  .output_limit = RATE_OUTPUT_LIMIT,
                  .d_filter_tau_s = RATE_D_FILTER_TAU_S},
                 {.kp = 0.124f,
                  .ki = 0.504f,
                  .kd = 0.00254f,
                  .integral_limit = RATE_INTEGRAL_LIMIT,
                  .output_limit = RATE_OUTPUT_LIMIT,
                  .d_filter_tau_s = RATE_D_FILTER_TAU_S},
                 {.kp = 0.154f,
                  .ki = 0.973f,
                  .integral_limit = RATE_INTEGRAL_LIMIT,
                  .output_limit = RATE_OUTPUT_LIMIT}},
        .max_rate_radps = {RATE_ROLL_MAX_RADPS, RATE_PITCH_MAX_RADPS,
                           RATE_YAW_MAX_RADPS},
        .period_us = RATE_PERIOD_US};
    if (app_configure_rate(&rate_config)) {
        APP_LOG_INFO("rate", "configured");
    } else {
        APP_LOG_ERROR("rate", "configuration_failed");
    }
#endif
    init_motor();
    init_imu();
#if APP_TELEMETRY_ENABLED || APP_LOG_ENABLED(APP_LOG_LEVEL_WARN)
    last_control_ms = HAL_GetTick();
#endif
#if APP_TELEMETRY_ENABLED
    last_stats_ms = last_control_ms;
    attitude_control_pending = false;
#endif
}

void app_process(const app_command_t *input) {
    command = input != NULL && input->valid ? *input : (app_command_t){0};
    if (command.mode == APP_COMMAND_SWITCH_LOW)
        tilt_target.have_update = false;
    process_imu();
    process_rate();
    process_motor();
#if APP_TELEMETRY_ENABLED
    if (imu.sample_valid)
        app_telemetry_queue_imu(&imu.sample, imu.sample_count);
#endif
    process_diagnostics();
}
