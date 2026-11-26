#include "app_motor.h"
#include "bsp_motor_pwm.h"
#include <stddef.h>

_Static_assert(APP_MOTOR_COUNT == BSP_MOTOR_PWM_MOTOR_COUNT,
               "Motor commands must match the board PWM channels");

bool app_motor_init(void) {
    return bsp_motor_pwm_init();
}

bool app_motor_set_all(const norm_q15_t command_norm[APP_MOTOR_COUNT]) {
    if (command_norm == NULL) {
        return false;
    }
    uint16_t pulse_us[APP_MOTOR_COUNT];
    for (unsigned i = 0; i < APP_MOTOR_COUNT; ++i) {
        const norm_q15_t command = command_norm[i];
        if (command < 0 || command > NORM_Q15_ONE) {
            return false;
        }
        const int32_t width = BSP_MOTOR_PWM_MIN_PULSE_US +
                              (command * (BSP_MOTOR_PWM_MAX_PULSE_US -
                                          BSP_MOTOR_PWM_MIN_PULSE_US) +
                               NORM_Q15_ONE / 2) /
                                  NORM_Q15_ONE;
        pulse_us[i] = (uint16_t)width;
    }
    return bsp_motor_pwm_set_all_us(pulse_us);
}

void app_motor_stop(void) {
    bsp_motor_pwm_stop();
}
