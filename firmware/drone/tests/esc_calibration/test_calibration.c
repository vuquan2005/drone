#include "app_safety.h"
#include "bsp_motor_pwm.h"
#include "esc_calibration.h"
#include <assert.h>
#include <stddef.h>

static uint32_t tick;
static bool init_ok, write_ok;
static unsigned stops, writes;
static uint16_t output;
uint32_t HAL_GetTick(void) {
    return tick;
}
bool bsp_motor_pwm_init(void) {
    if (init_ok)
        output = 1000;
    return init_ok;
}
bool bsp_motor_pwm_set_all_us(const uint16_t pulses[4]) {
    writes++;
    for (unsigned i = 0; i < 4; i++)
        assert(pulses[i] == pulses[0]);
    if (write_ok)
        output = pulses[0];
    return write_ok;
}
void bsp_motor_pwm_stop(void) {
    stops++;
    output = 0;
}
static app_command_t command;
static void reset(void) {
    init_ok = write_ok = true;
    writes = stops = 0;
    command = (app_command_t){.valid = true, .esc_calibration_requested = true};
    esc_calibration_init();
    assert(output == 1000 &&
           esc_calibration_get_state() == ESC_CALIBRATION_READY);
}
static void maximum(void) {
    esc_calibration_process(&command);
    assert(output == 2000 &&
           esc_calibration_get_state() == ESC_CALIBRATION_MAX);
}
int main(void) {
    tick = UINT32_MAX - 1000;
    reset();
    maximum();
    tick += 2000;
    command.esc_calibration_requested = false;
    esc_calibration_process(&command);
    assert(output == 1000 &&
           esc_calibration_get_state() == ESC_CALIBRATION_MIN);
    command.esc_calibration_requested = true;
    tick += ESC_CALIBRATION_MIN_HOLD_MS - 1;
    esc_calibration_process(&command);
    assert(esc_calibration_get_state() == ESC_CALIBRATION_MIN);
    tick++;
    esc_calibration_process(&command);
    assert(esc_calibration_get_state() == ESC_CALIBRATION_DONE);
    unsigned held = writes;
    esc_calibration_process(NULL);
    assert(writes == held && output == 1000);
    for (unsigned cause = 0; cause < 4; cause++) {
        reset();
        maximum();
        if (cause == 0)
            command.valid = false;
        if (cause == 1)
            command.arm_requested = true;
        if (cause == 2)
            command.throttle = APP_ARM_THROTTLE_MAX + 1;
        if (cause == 3)
            tick += ESC_CALIBRATION_MAX_HOLD_MS;
        esc_calibration_process(&command);
        assert(output == 1000 &&
               esc_calibration_get_state() == ESC_CALIBRATION_ABORT_MIN);
        assert(esc_calibration_get_reason() ==
               (cause == 0   ? ESC_CALIBRATION_RC_LOST
                : cause == 1 ? ESC_CALIBRATION_ARM_REQUESTED
                : cause == 2 ? ESC_CALIBRATION_THROTTLE_HIGH
                             : ESC_CALIBRATION_TIMEOUT));
        command =
            (app_command_t){.valid = true, .esc_calibration_requested = true};
        esc_calibration_process(&command);
        assert(output == 1000);
    }
    reset();
    command.esc_calibration_requested = false;
    esc_calibration_process(&command);
    assert(esc_calibration_get_reason() == ESC_CALIBRATION_CANCELLED &&
           output == 1000);
    reset();
    maximum();
    esc_calibration_process(NULL);
    assert(esc_calibration_get_reason() == ESC_CALIBRATION_RC_LOST &&
           output == 1000);
    reset();
    write_ok = false;
    esc_calibration_process(&command);
    assert(stops == 1 && output == 0 &&
           esc_calibration_get_state() == ESC_CALIBRATION_PWM_ERROR);
    write_ok = true;
    esc_calibration_process(&command);
    assert(output == 0);
    reset();
    maximum();
    write_ok = false;
    command.valid = false;
    esc_calibration_process(&command);
    assert(output == 0 &&
           esc_calibration_get_state() == ESC_CALIBRATION_PWM_ERROR);
    init_ok = false;
    esc_calibration_init();
    assert(output == 0 &&
           esc_calibration_get_reason() == ESC_CALIBRATION_PWM_FAILED);
    return 0;
}
