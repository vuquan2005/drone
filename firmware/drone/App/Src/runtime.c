#include "runtime.h"
#include "app.h"
#include "app_log.h"
#include "app_receiver.h"
#include "app_telemetry.h"
#include "bsp_motor_pwm.h"
#include "esc_calibration.h"
#include "main.h"
#include <stddef.h>

static runtime_mode_t mode;
static app_command_t command;
static app_receiver_sample_t receiver_sample;
static uint32_t receiver_sequence;
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
#if DRONE_RTT && DRONE_TELEMETRY
    last_receiver_ms = HAL_GetTick();
#endif
    APP_LOG_DEBUG("runtime", "waiting for boot mode selection");
}

bool runtime_mode_supported(void) {
    return mode == RUNTIME_FLIGHT || mode == RUNTIME_ESC_CALIB;
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
#else
    if (!command.valid) {
        return RUNTIME_BOOT_SELECT;
    }
    if (command.esc_calibration_requested) {
        return RUNTIME_ESC_CALIB;
    }
    if (command.telemetry_mode == APP_COMMAND_TELEMETRY_STREAM) {
        return RUNTIME_FLASH_EXPORT;
    }
    return RUNTIME_FLIGHT;
#endif
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
        };
        if (mode != RUNTIME_BOOT_SELECT)
            APP_LOG_INFO("runtime", "selected mode=%s supported=%u",
                         mode_names[mode], (unsigned)runtime_mode_supported());
#endif
        if (mode == RUNTIME_FLIGHT) {
            app_init();
        } else if (mode == RUNTIME_ESC_CALIB) {
            esc_calibration_init();
        }
        /* Initialization can block: never forward the pre-init RC snapshot. */
        return;
    }
    switch (mode) {
    case RUNTIME_FLIGHT:
        app_process(&command);
        break;
    case RUNTIME_ESC_CALIB:
        esc_calibration_process(&command);
        break;
    case RUNTIME_FLASH_EXPORT:
        break;
    case RUNTIME_BOOT_SELECT:
        break;
    }
}
