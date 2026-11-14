#include "app_motor.h"
#include "bsp_pwm.h"
#include <stddef.h>

_Static_assert(APP_MOTOR_COUNT == BSP_PWM_CHANNEL_COUNT,
               "Motor commands must match the board PWM channels");

bool app_motor_init(void) {
    return bsp_pwm_init();
}

bool app_motor_set_all(const float command_norm[APP_MOTOR_COUNT]) {
    if (command_norm == NULL) {
        return false;
    }
    uint16_t pulse_us[APP_MOTOR_COUNT];
    for (unsigned i = 0; i < APP_MOTOR_COUNT; ++i) {
        const float command = command_norm[i];
        /* This form also rejects NaN before converting to an integer. */
        if (!(command >= 0.0f && command <= 1.0f)) {
            return false;
        }
        const float width =
            BSP_PWM_MIN_PULSE_US +
            command * (BSP_PWM_MAX_PULSE_US - BSP_PWM_MIN_PULSE_US);
        pulse_us[i] = (uint16_t)(width + 0.5f);
    }
    return bsp_pwm_set_all_us(pulse_us);
}

void app_motor_stop(void) {
    bsp_pwm_stop();
}
