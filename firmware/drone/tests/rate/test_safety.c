#include "app_safety.h"
#include <assert.h>

static void step(app_safety_t *s, app_command_t *c) {
    app_safety_update(s, c, true, true, true, 0U);
}

static void test_idle_timeout(void) {
    app_safety_t s = {0};
    app_command_t c = {.valid = true, .throttle = APP_ARM_THROTTLE_MAX};
    const uint32_t timeout_ms = APP_ARM_IDLE_TIMEOUT_MS;
    const uint32_t low_ms = UINT32_MAX - timeout_ms / 2U;
    const uint32_t deadline_ms = low_ms + timeout_ms;

    step(&s, &c);
    c.arm_requested = true;
    step(&s, &c);
    assert(s.state == APP_ARMED);

    c.throttle++;
    assert(app_safety_update(&s, &c, true, true, true, timeout_ms - 1U) ==
           APP_SAFETY_OK);
    assert(app_safety_update(&s, &c, true, true, true, low_ms - 1U) ==
           APP_SAFETY_OK);
    assert(s.state == APP_ARMED);

    c.throttle--;
    assert(app_safety_update(&s, &c, true, true, true, low_ms) ==
           APP_SAFETY_OK);
    assert(app_safety_update(&s, &c, true, true, true, deadline_ms - 1U) ==
           APP_SAFETY_OK);
    c.throttle++;
    assert(app_safety_update(&s, &c, true, true, true, deadline_ms) ==
           APP_SAFETY_IDLE_TIMEOUT);
    assert(s.state == APP_DISARMED && s.reason == APP_SAFETY_IDLE_TIMEOUT);

    c.throttle--;
    assert(app_safety_update(&s, &c, true, true, true, deadline_ms + 1U) ==
           APP_SAFETY_ARM_OFF_REQUIRED);
    assert(s.state == APP_DISARMED && s.reason == APP_SAFETY_IDLE_TIMEOUT);

    c.arm_requested = false;
    app_safety_update(&s, &c, true, true, true, deadline_ms + 2U);
    c.arm_requested = true;
    const uint32_t rearm_ms = deadline_ms + 3U;
    assert(app_safety_update(&s, &c, true, true, true, rearm_ms) ==
           APP_SAFETY_OK);
    assert(s.state == APP_ARMED && s.reason == APP_SAFETY_OK);
    assert(app_safety_update(&s, &c, false, true, true,
                             rearm_ms + timeout_ms) == APP_SAFETY_IMU);
    assert(s.state == APP_FAILSAFE && s.reason == APP_SAFETY_IMU);
}

int main(void) {
    test_idle_timeout();
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
        app_safety_update(&s, &c, fault != 1, fault != 2, fault != 3, 0U);
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
