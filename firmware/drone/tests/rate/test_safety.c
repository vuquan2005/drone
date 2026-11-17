#include "app_safety.h"
#include <assert.h>

static void step(app_safety_t *s, app_command_t *c) {
    app_safety_update(s, c, true, true, true);
}
int main(void) {
    app_safety_t s = {0};
    app_command_t c = {.valid = true, .arm_requested = true};
    step(&s, &c);
    assert(s.state == APP_DISARMED);
    c.arm_requested = false;
    step(&s, &c);
    c.arm_requested = true;
    c.throttle = APP_ARM_THROTTLE_MAX + 1;
    step(&s, &c);
    assert(s.state == APP_DISARMED);
    c.throttle = 0;
    step(&s, &c);
    assert(s.state == APP_DISARMED);
    c.arm_requested = false;
    step(&s, &c);
    c.arm_requested = true;
    step(&s, &c);
    assert(s.state == APP_ARMED);
    c.arm_requested = false;
    step(&s, &c);
    assert(s.state == APP_DISARMED);
    c.arm_requested = true;
    step(&s, &c);
    assert(s.state == APP_ARMED);
    for (unsigned fault = 0; fault < 5; ++fault) {
        c.valid = fault != 0;
        c.esc_calibration_requested = fault == 4;
        app_safety_update(&s, &c, fault != 1, fault != 2, fault != 3);
        assert(s.state == APP_FAILSAFE && !s.off_seen);
        c.valid = true;
        c.esc_calibration_requested = false;
        step(&s, &c);
        assert(s.state == APP_FAILSAFE);
        c.arm_requested = false;
        step(&s, &c);
        c.arm_requested = true;
        step(&s, &c);
        assert(s.state == APP_ARMED);
    }
    return 0;
}
