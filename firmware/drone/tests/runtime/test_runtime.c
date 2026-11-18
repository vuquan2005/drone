#include "app.h"
#include "bsp_motor_pwm.h"
#include "esc_calibration.h"
#include "runtime.h"
#include <assert.h>
#include <stddef.h>

uint32_t HAL_GetTick(void) {
    return 1000U;
}
static app_receiver_sample_t input;
static bool available;
static bool publish_update = true;
static bool disconnect_during_init;
static unsigned inits, polls, starts, processes, stops, calib_inits,
    calib_polls;
void esc_calibration_init(void) {
    calib_inits++;
}
void esc_calibration_process(const app_command_t *command) {
    assert(command != NULL);
    calib_polls++;
}

void app_receiver_init(void) {
    inits++;
}
bool app_receiver_get_sample(app_receiver_sample_t *sample) {
    polls++;
    *sample = input;
    return available;
}
bool app_receiver_update_sample(app_receiver_sample_t *sample,
                                uint32_t *sequence) {
    if (!publish_update) {
        polls++;
        return false;
    }
    (*sequence)++;
    if (!app_receiver_get_sample(sample)) {
        *sample = (app_receiver_sample_t){0};
    }
    return true;
}
void bsp_motor_pwm_stop(void) {
    stops++;
}
void app_init(void) {
    starts++;
    if (disconnect_during_init)
        input.connected = false;
}
void app_process(const app_command_t *command) {
    processes++;
    assert(command != NULL);
    assert(command->valid == (available && input.connected));
    if (command->valid)
        assert(command->throttle == 0);
}

static void reset(void) {
    input = (app_receiver_sample_t){0};
    for (unsigned i = 0; i < CRSF_CHANNEL_COUNT; i++)
        input.channels[i] = 992;
    input.channels[2] = input.channels[4] = input.channels[5] =
        input.channels[6] = input.channels[7] = 172;
    input.connected = true;
    available = true;
    publish_update = true;
    disconnect_during_init = false;
    inits = polls = starts = processes = stops = calib_inits = calib_polls = 0;
    runtime_init();
    assert(inits == 1 && stops == 1 && starts == 0);
    assert(runtime_get_mode() == RUNTIME_BOOT_SELECT);
    assert(!runtime_mode_supported());
}

int main(void) {
    reset();
    available = false;
    runtime_process();
    assert(runtime_get_mode() == RUNTIME_BOOT_SELECT && starts == 0);
    input.connected = false;
    available = true;
    runtime_process();
    assert(runtime_get_mode() == RUNTIME_BOOT_SELECT && starts == 0);
    input.connected = true;
    input.channels[2] = input.channels[4] = 1811;
    input.channels[5] = 992;
    runtime_process();
    assert(runtime_get_mode() == RUNTIME_FLIGHT && starts == 1 &&
           processes == 0);
    input.channels[2] = 172;
    input.channels[5] = input.channels[6] = 1811;
    assert(runtime_mode_supported());
    runtime_process();
    assert(runtime_get_mode() == RUNTIME_FLIGHT && starts == 1 &&
           processes == 1);
    available = false;
    runtime_process();
    app_command_t command;
    assert(runtime_get_command(&command) && !command.valid);
    assert(!runtime_get_command(NULL));
    assert(processes == 2 && inits == 1 && polls == 5);

    /* SC never selects maintenance or conflicts with SB, at any position. */
    const uint16_t positions[] = {172, 992, 1811};
    for (unsigned i = 0; i < 3; ++i) {
        reset();
        input.channels[5] = positions[i];
        runtime_process();
        assert(runtime_get_mode() == RUNTIME_FLIGHT && starts == 1);
        runtime_process();
        assert(runtime_get_command(&command));
        assert(command.mode == (app_command_switch_t)i);
        reset();
        input.channels[5] = positions[i];
        input.channels[6] = 1811;
        runtime_process();
        assert(runtime_get_mode() == RUNTIME_ESC_CALIB &&
               runtime_mode_supported());
        assert(calib_inits == 1 && calib_polls == 0);
        input.channels[6] = input.channels[7] = 172;
        runtime_process();
        assert(runtime_get_mode() == RUNTIME_ESC_CALIB && starts == 0 &&
               processes == 0);
        assert(stops == 1 && calib_inits == 1 && calib_polls == 1);
    }
    reset();
    input.channels[2] = input.channels[4] = input.channels[7] = 1811;
    runtime_process();
    assert(runtime_get_mode() == RUNTIME_FLASH_EXPORT);
    assert(!runtime_mode_supported() && starts == 0 && processes == 0);
    input.channels[7] = 172;
    input.connected = false;
    runtime_process();
    assert(runtime_get_mode() == RUNTIME_FLASH_EXPORT && stops == 1);
    reset();
    input.channels[2] = input.channels[4] = input.channels[6] =
        input.channels[7] = 1811;
    input.connected = false;
    runtime_process();
    assert(runtime_get_mode() == RUNTIME_BOOT_SELECT && calib_inits == 0);
    input.connected = true;
    runtime_process();
    assert(runtime_get_mode() == RUNTIME_ESC_CALIB && calib_inits == 1 &&
           starts == 0);
    reset();
    input.channels[7] = 992;
    runtime_process();
    assert(runtime_get_mode() == RUNTIME_FLIGHT);
    reset();
    disconnect_during_init = true;
    runtime_process();
    assert(starts == 1 && processes == 0);
    runtime_process();
    assert(processes == 1);
    assert(runtime_get_command(&command) && !command.valid);
    reset();
    runtime_process();
    runtime_process();
    assert(runtime_get_command(&command) && command.roll == 0);
    publish_update = false;
    input.channels[0] = 1811;
    runtime_process();
    assert(runtime_get_command(&command) && command.roll == 0);
    publish_update = true;
    runtime_process();
    assert(runtime_get_command(&command) && command.roll == NORM_Q15_ONE);
    input.connected = false;
    runtime_process();
    assert(runtime_get_command(&command) && !command.valid &&
           command.roll == 0);
    publish_update = false;
    runtime_process();
    assert(runtime_get_command(&command) && !command.valid);
    return 0;
}
