#include "runtime.h"
#include "accel_calibration.h"
#include "app.h"
#include "app_log.h"
#include "app_receiver.h"
#include "app_telemetry.h"
#include "bsp_motor_pwm.h"
#include "esc_calibration.h"
#include "flash_erase.h"
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
#if APP_TELEMETRY_ENABLED
static uint32_t last_receiver_ms;
#endif
#if APP_TELEMETRY_RTT_ENABLED
static uint32_t last_config_poll_ms;
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
#if APP_TELEMETRY_ENABLED
    last_receiver_ms = last_crsf_status_ms;
#endif
#if APP_TELEMETRY_RTT_ENABLED
    last_config_poll_ms = last_crsf_status_ms;
#endif
    APP_LOG_INFO("runtime", "boot_select state=waiting_for_rc");
}

bool runtime_mode_supported(void) {
    return mode == RUNTIME_FLIGHT || mode == RUNTIME_ESC_CALIB ||
           mode == RUNTIME_IMU_CALIB || mode == RUNTIME_FLASH_ERASE;
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
    // return RUNTIME_FLIGHT; // For debugging, force flight mode without RC
    // input
#endif
    if (!command.valid) {
        return RUNTIME_BOOT_SELECT;
    }
    if (command.esc_calibration_requested &&
        command.telemetry_mode == APP_COMMAND_TELEMETRY_STREAM) {
        return RUNTIME_FLASH_ERASE;
    }
    if (command.imu_calibration_requested) {
        return RUNTIME_IMU_CALIB;
    }
    if (command.esc_calibration_requested) {
        return RUNTIME_ESC_CALIB;
    }
    if (command.telemetry_mode == APP_COMMAND_TELEMETRY_STREAM) {
        return RUNTIME_BOOT_SELECT;
    }
    return RUNTIME_FLIGHT;
}

static const char *crsf_flight_status_label(void) {
    app_status_t status;
    (void)app_get_status(&status);
    switch (status.arm_state) {
    case APP_DISARMED:
        return status.safety_reason == APP_SAFETY_IDLE_TIMEOUT ? "IDLE TIMEOUT"
                                                               : "DISARMED";
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

static const char *crsf_imu_calibration_label(char *progress) {
    const imu_calibration_status_t status = imu_calibration_get_status();
    switch (status.state) {
    case IMU_CALIBRATION_IDLE:
        return "IMU CAL";
    case IMU_CALIBRATION_GYRO:
        return "IMU GYRO";
    case IMU_CALIBRATION_ACCEL: {
        unsigned count = 0U;
        unsigned next_face = ACCEL_CAL_NO_FACE;
        for (unsigned face = 0U; face < ACCEL_CAL_NO_FACE; face++) {
            if ((status.captured_faces & (1U << face)) != 0U) {
                count++;
            } else if (next_face == ACCEL_CAL_NO_FACE) {
                next_face = face;
            }
        }
        if (next_face == ACCEL_CAL_NO_FACE)
            return "ACC 6/6";
        /* The face enum lists the positive and negative faces of each axis. */
        progress[0] = "XYZ"[next_face / 2U];
        progress[1] = next_face % 2U == 0U ? '+' : '-';
        progress[3] = (char)('0' + count);
        return progress;
    }
    case IMU_CALIBRATION_DONE:
        return "IMU DONE";
    case IMU_CALIBRATION_RC_LOST:
        return "IMU RC";
    case IMU_CALIBRATION_CANCELLED:
        return "IMU STOP";
    case IMU_CALIBRATION_INIT_FAILED:
        return "IMU INIT";
    case IMU_CALIBRATION_READ_FAILED:
        return "IMU READ";
    case IMU_CALIBRATION_TIMEOUT:
        return "IMU TIME";
    case IMU_CALIBRATION_INVALID_ACCEL:
        return "ACC ERR";
    case IMU_CALIBRATION_SAVE_FAILED:
        return "SAVE ERR";
    }
    return NULL;
}

static const char *crsf_status_label(char *imu_progress) {
    switch (mode) {
    case RUNTIME_BOOT_SELECT:
        return command.valid ? "WAIT SD" : "WAIT RC";
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
    case RUNTIME_FLASH_ERASE:
        switch (flash_erase_get_state()) {
        case FLASH_ERASE_WAIT:
            return "ERASE WAIT";
        case FLASH_ERASE_BUSY:
            return "ERASING";
        case FLASH_ERASE_DONE:
            return "ERASE DONE";
        case FLASH_ERASE_ERROR:
            return "ERASE ERR";
        }
        break;
    case RUNTIME_IMU_CALIB:
        return crsf_imu_calibration_label(imu_progress);
    }
    return NULL;
}

static void process_crsf_telemetry(uint32_t now_ms) {
    const uint32_t status_age_ms = now_ms - last_crsf_status_ms;
    if (status_age_ms >= CRSF_STATUS_PERIOD_MS) {
        last_crsf_status_ms = now_ms;
        char imu_progress[] = "X+ 0/6";
        (void)app_receiver_send_flight_mode(crsf_status_label(imu_progress));
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

#if APP_TELEMETRY_RTT_ENABLED
static void process_config_requests(uint32_t now_ms) {
    if (mode == RUNTIME_BOOT_SELECT || now_ms - last_config_poll_ms < 20U)
        return;
    last_config_poll_ms = now_ms;
    uint32_t id;
    if (!app_telemetry_poll_config_request(&id))
        return;
    app_config_t config = {0};
    switch (mode) {
    case RUNTIME_FLIGHT:
        config = app_get_config();
        break;
    case RUNTIME_IMU_CALIB:
        config = imu_calibration_get_config();
        break;
    case RUNTIME_ESC_CALIB:
        config.mode = APP_CONFIG_ESC_CALIB;
        break;
    case RUNTIME_BOOT_SELECT:
    case RUNTIME_FLASH_ERASE:
        return;
    }
    (void)app_telemetry_write_config(id, &config);
}
#endif

void runtime_process(void) {
    if (app_receiver_update_sample(&receiver_sample, &receiver_sequence)) {
        (void)app_command_decode(&receiver_sample, &command);
    }
#if DRONE_FLASH_RECORD
    if (mode == RUNTIME_FLIGHT) {
        const bool requested =
            command.telemetry_mode == APP_COMMAND_TELEMETRY_RECORD;
        if (command.valid && !requested)
            app_telemetry_flush_recording_imu();
        app_flash_recorder_update(command.valid, requested, HAL_GetTick());
    }
#endif
#if APP_TELEMETRY_ENABLED
    const uint32_t now = HAL_GetTick();
    if ((uint32_t)(now - last_receiver_ms) >= 50U) {
        last_receiver_ms = now;
        (void)app_telemetry_write_receiver(&receiver_sample, now);
    }
#endif
    if (mode == RUNTIME_BOOT_SELECT) {
        mode = select_mode();
#if APP_LOG_ENABLED(APP_LOG_LEVEL_INFO)
        static const char *const mode_names[] = {
            [RUNTIME_FLIGHT] = "flight",
            [RUNTIME_ESC_CALIB] = "esc_calib",
            [RUNTIME_IMU_CALIB] = "imu_calib",
            [RUNTIME_FLASH_ERASE] = "flash_erase",
        };
        if (mode != RUNTIME_BOOT_SELECT)
            APP_LOG_INFO("runtime", "selected mode=%s", mode_names[mode]);
#endif
        if (mode == RUNTIME_FLIGHT) {
            /* Initialization can block; poll fresh RC before dispatch. */
            app_init();
#if DRONE_FLASH_RECORD
            app_flash_recorder_init(HAL_GetTick());
#endif
            return;
        }
        if (mode == RUNTIME_ESC_CALIB) {
            esc_calibration_init();
            return;
        }
        if (mode == RUNTIME_FLASH_ERASE) {
            flash_erase_rc_init();
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
    case RUNTIME_FLASH_ERASE:
        flash_erase_rc_process(&command, HAL_GetTick());
        break;
    case RUNTIME_BOOT_SELECT:
        break;
    }
    app_telemetry_process_imu();
#if DRONE_FLASH_RECORD
    if (mode == RUNTIME_FLIGHT)
        app_flash_recorder_process(HAL_GetTick(), app_get_config);
#endif
#if APP_TELEMETRY_RTT_ENABLED
    process_config_requests(now);
#endif
    process_crsf_telemetry(HAL_GetTick());
}
