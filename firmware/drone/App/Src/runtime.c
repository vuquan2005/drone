#include "runtime.h"
#include "app.h"
#include "app_log.h"
#include "app_receiver.h"
#include "app_telemetry.h"
#include "bsp_motor_pwm.h"
#include "esc_calibration.h"
#include "imu_calibration.h"
#include "main.h"
#include <stddef.h>

#define CRSF_STATUS_PERIOD_MS 500U
#define CRSF_ATTITUDE_PERIOD_MS 200U
#define CRSF_TX_GUARD_MS 2U

static runtime_mode_t mode;
static app_command_t command;
static app_receiver_sample_t receiver_sample;
static uint32_t receiver_sequence;
static uint32_t last_crsf_status_ms;
static uint32_t last_crsf_attitude_ms;
#if DRONE_RTT && DRONE_TELEMETRY
static uint32_t last_receiver_ms;
#endif

void runtime_init(void) {
    mode = RUNTIME_BOOT_SELECT;
    command = (app_command_t){0};
    receiver_sample = (app_receiver_sample_t){0};
    receiver_sequence = 0U;
    /* Establish safe outputs only; the selected mode owns PWM startup. */
    bsp_motor_pwm_stop();
    bsp_rtt_init();
    app_telemetry_init();
    app_log_init(HAL_GetTick);
    app_receiver_init();
    last_crsf_status_ms = HAL_GetTick();
    last_crsf_attitude_ms = last_crsf_status_ms;
#if DRONE_RTT && DRONE_TELEMETRY
    last_receiver_ms = last_crsf_status_ms;
#endif
    APP_LOG_DEBUG("runtime", "waiting for boot mode selection");
}

bool runtime_mode_supported(void) {
    return mode == RUNTIME_FLIGHT || mode == RUNTIME_ESC_CALIB ||
           mode == RUNTIME_IMU_CALIB;
}

runtime_mode_t runtime_get_mode(void) {
    return mode;
}

bool runtime_get_command(app_command_t *out) {
    if (out == NULL) {
        return false;
    }
    *out = command;
    return true;
}

static runtime_mode_t select_mode(void) {
#if DRONE_DEBUG_FORCE_FLIGHT
    return RUNTIME_FLIGHT;
#endif
    if (!command.valid) {
        return RUNTIME_BOOT_SELECT;
    }
    if (command.imu_calibration_requested) {
        return RUNTIME_IMU_CALIB;
    }
    if (command.esc_calibration_requested) {
        return RUNTIME_ESC_CALIB;
    }
    if (command.telemetry_mode == APP_COMMAND_TELEMETRY_STREAM) {
        return RUNTIME_FLASH_EXPORT;
    }
    return RUNTIME_FLIGHT;
}

static const char *crsf_flight_status_label(void) {
    app_status_t status;
    (void)app_get_status(&status);
    switch (status.arm_state) {
    case APP_DISARMED:
        return "DISARMED";
    case APP_ARMED:
        return "ARMED";
    case APP_FAILSAFE:
        switch (status.safety_reason) {
        case APP_SAFETY_IMU:
            return status.imu_initialized ? "FS IMU" : "IMU INIT";
        case APP_SAFETY_RC:
            return "FS RC";
        case APP_SAFETY_PWM:
            return "FS PWM";
        case APP_SAFETY_CONTROL:
            return "FS CTRL";
        default:
            return "FAILSAFE";
        }
    }
    return "FAILSAFE";
}

static const char *crsf_status_label(void) {
    switch (mode) {
    case RUNTIME_BOOT_SELECT:
        return "WAIT RC";
    case RUNTIME_FLIGHT:
        return crsf_flight_status_label();
    case RUNTIME_ESC_CALIB:
        switch (esc_calibration_get_state()) {
        case ESC_CALIBRATION_IDLE:
        case ESC_CALIBRATION_READY:
            return "ESC READY";
        case ESC_CALIBRATION_MAX:
            return "ESC MAX";
        case ESC_CALIBRATION_MIN:
            return "ESC MIN";
        case ESC_CALIBRATION_DONE:
            return "ESC DONE";
        case ESC_CALIBRATION_ABORT_MIN:
            return "ESC ABORT";
        case ESC_CALIBRATION_PWM_ERROR:
            return "ESC PWM ERR";
        }
        break;
    case RUNTIME_FLASH_EXPORT:
        return "FLASH N/A";
    case RUNTIME_IMU_CALIB:
        return "IMU CAL";
    }
    return NULL;
}

static void process_crsf_telemetry(uint32_t now_ms) {
    const uint32_t status_age_ms = now_ms - last_crsf_status_ms;
    if (status_age_ms >= CRSF_STATUS_PERIOD_MS) {
        last_crsf_status_ms = now_ms;
        (void)app_receiver_send_flight_mode(crsf_status_label());
        return;
    }

    /* The UART has one TX buffer. Leave room on both sides of FM for an
     * interrupt-driven frame to complete (<0.5 ms on the wire at 420 kbaud). */
    if (mode != RUNTIME_FLIGHT ||
        now_ms - last_crsf_attitude_ms < CRSF_ATTITUDE_PERIOD_MS ||
        status_age_ms < CRSF_TX_GUARD_MS ||
        status_age_ms > CRSF_STATUS_PERIOD_MS - CRSF_TX_GUARD_MS)
        return;

    last_crsf_attitude_ms = now_ms;
    float body_to_ned[4];
    if (app_get_attitude(body_to_ned))
        (void)app_receiver_send_attitude(body_to_ned);
}

void runtime_process(void) {
    if (app_receiver_update_sample(&receiver_sample, &receiver_sequence)) {
        (void)app_command_decode(&receiver_sample, &command);
    }
#if DRONE_RTT && DRONE_TELEMETRY
    const uint32_t now = HAL_GetTick();
    if ((uint32_t)(now - last_receiver_ms) >= 50U) {
        last_receiver_ms = now;
        APP_TELEMETRY_RECEIVER(&receiver_sample, now);
    }
#endif
    if (mode == RUNTIME_BOOT_SELECT) {
        mode = select_mode();
#if DRONE_RTT && DRONE_LOG_LEVEL >= APP_LOG_LEVEL_INFO
        static const char *const mode_names[] = {
            [RUNTIME_FLIGHT] = "flight",
            [RUNTIME_ESC_CALIB] = "esc_calib",
            [RUNTIME_FLASH_EXPORT] = "flash_export",
            [RUNTIME_IMU_CALIB] = "imu_calib",
        };
        if (mode != RUNTIME_BOOT_SELECT)
            APP_LOG_INFO("runtime", "selected mode=%s supported=%u",
                         mode_names[mode], (unsigned)runtime_mode_supported());
#endif
        if (mode == RUNTIME_FLIGHT) {
            /* Initialization can block; poll fresh RC before dispatch. */
            app_init();
            return;
        }
        if (mode == RUNTIME_ESC_CALIB) {
            esc_calibration_init();
            return;
        }
        if (mode == RUNTIME_IMU_CALIB) {
            imu_calibration_init();
            /* The sensor init can outlast the RC snapshot. Require a frame
             * decoded by a later poll before qualifying calibration. */
            command = (app_command_t){0};
            return;
        }
    }
    switch (mode) {
    case RUNTIME_FLIGHT:
        app_process(&command);
        break;
    case RUNTIME_ESC_CALIB:
        esc_calibration_process(&command);
        break;
    case RUNTIME_IMU_CALIB:
        imu_calibration_process(&command);
        break;
    case RUNTIME_FLASH_EXPORT:
        break;
    case RUNTIME_BOOT_SELECT:
        break;
    }
    process_crsf_telemetry(HAL_GetTick());
}
