#include "status_led.h"
#include "bsp_status_led.h"

typedef struct {
    uint16_t on_ms;
    uint16_t gap_ms;
    uint16_t pause_ms;
    uint8_t pulses;
} blink_pattern_t;

static const blink_pattern_t patterns[] = {
    [STATUS_LED_WAITING] = {100U, 0U, 1900U, 1U},
    [STATUS_LED_DISARMED] = {500U, 0U, 500U, 1U},
    [STATUS_LED_MAINTENANCE] = {350U, 200U, 1200U, 2U},
    [STATUS_LED_RC_LOSS] = {120U, 120U, 1200U, 2U},
    [STATUS_LED_IMU_FAULT] = {120U, 120U, 1200U, 3U},
    [STATUS_LED_MOTOR_FAULT] = {120U, 120U, 1200U, 4U},
    [STATUS_LED_OTHER_FAULT] = {120U, 120U, 1200U, 5U},
};

static enum {
    STARTUP_IDLE,
    STARTUP_PULSE,
    STARTUP_GAP,
} startup_phase;
static bool pattern_active;
static bool led_on;
static uint32_t startup_ms;
static uint32_t pattern_ms;
static status_led_pattern_t active_pattern;

static void set_led(bool on) {
    if (on != led_on) {
        bsp_status_led_write(on);
        led_on = on;
    }
}

void status_led_startup(uint32_t now_ms) {
    startup_ms = now_ms;
    startup_phase = STARTUP_PULSE;
    pattern_active = false;
    set_led(true);
}

void status_led_update(status_led_pattern_t pattern, uint32_t now_ms) {
    if (pattern >= STATUS_LED_RC_LOSS) {
        startup_phase = STARTUP_IDLE;
    }
    if (startup_phase == STARTUP_PULSE) {
        if (now_ms - startup_ms >= 250U) {
            set_led(false);
            startup_phase = STARTUP_GAP;
            startup_ms = now_ms;
        }
        return;
    }
    if (startup_phase == STARTUP_GAP) {
        if (now_ms - startup_ms < 150U)
            return;
        startup_phase = STARTUP_IDLE;
    }

    if (!pattern_active || pattern != active_pattern) {
        pattern_active = true;
        active_pattern = pattern;
        pattern_ms = now_ms;
    }

    if (pattern == STATUS_LED_ARMED) {
        set_led(true);
        return;
    }

    const blink_pattern_t *blink = &patterns[pattern];
    const uint32_t pulse_ms = (uint32_t)blink->on_ms + blink->gap_ms;
    const uint32_t group_ms = pulse_ms * blink->pulses - blink->gap_ms;
    const uint32_t period_ms = group_ms + blink->pause_ms;
    const uint32_t phase_ms = (now_ms - pattern_ms) % period_ms;
    pattern_ms = now_ms - phase_ms;
    set_led(phase_ms < group_ms && phase_ms % pulse_ms < blink->on_ms);
}
