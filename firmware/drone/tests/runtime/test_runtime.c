#include "app.h"
#include "bsp_motor_pwm.h"
#include "esc_calibration.h"
#include "imu_calibration.h"
#include "runtime.h"
#include <assert.h>
#include <stddef.h>
#include <string.h>

static uint32_t tick = 1000U;
uint32_t HAL_GetTick(void) {
    return tick;
}
static app_receiver_sample_t input;
static bool available;
static bool publish_update = true;
static bool disconnect_during_init;
static unsigned inits, polls, starts, processes, stops, calib_inits,
    calib_polls;
static unsigned imu_calib_inits, imu_calib_polls;
static app_command_t imu_calib_command;
static app_arm_state_t arm_state;
static esc_calibration_state_t calib_state;
static unsigned status_attempts;
static const char *status_label;
static bool status_tx_ready;
void esc_calibration_init(void) {
    calib_inits++;
}
void esc_calibration_process(const app_command_t *command) {
    assert(command != NULL);
    calib_polls++;
}
void imu_calibration_init(void) {
    imu_calib_inits++;
}
void imu_calibration_process(const app_command_t *command) {
    assert(command != NULL);
    imu_calib_polls++;
    imu_calib_command = *command;
}
esc_calibration_state_t esc_calibration_get_state(void) {
    assert(calib_inits == 1 && calib_polls != 0);
    return calib_state;
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
bool app_receiver_send_flight_mode(const char *label) {
    assert(label != NULL);
    status_attempts++;
    if (!status_tx_ready)
        return false;
    status_label = label;
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
    else
        arm_state = APP_FAILSAFE;
}
app_arm_state_t app_get_arm_state(void) {
    assert(starts == 1 && processes != 0);
    return arm_state;
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
    imu_calib_inits = imu_calib_polls = 0;
    arm_state = APP_DISARMED;
    calib_state = ESC_CALIBRATION_READY;
    status_attempts = 0;
    status_label = NULL;
    status_tx_ready = true;
    runtime_init();
    assert(inits == 1 && stops == 1 && starts == 0);
    assert(runtime_get_mode() == RUNTIME_BOOT_SELECT);
    assert(!runtime_mode_supported());
}

static void expect_next_status(const char *expected) {
    const unsigned previous_attempts = status_attempts;
    tick += 500U;
    runtime_process();
    assert(status_attempts == previous_attempts + 1U);
    assert(strcmp(status_label, expected) == 0);
}

static void test_crsf_status(void) {
    tick = UINT32_MAX - 249U;
    reset();
    input.connected = false;
    tick += 499U;
    runtime_process();
    assert(status_attempts == 0);
    tick++;
    runtime_process();
    assert(status_attempts == 1 && strcmp(status_label, "WAIT RC") == 0);
    input.connected = true;
    runtime_process();
    expect_next_status("DISARMED");
    arm_state = APP_ARMED;
    expect_next_status("ARMED");

    status_tx_ready = false;
    tick += 500U;
    runtime_process();
    assert(status_attempts == 4);
    status_tx_ready = true;
    runtime_process();
    assert(status_attempts == 4);
    input.connected = false;
    expect_next_status("FAILSAFE");

    reset();
    input.channels[6] = 1811;
    runtime_process();
    expect_next_status("ESC READY");
    calib_state = ESC_CALIBRATION_MAX;
    expect_next_status("ESC MAX");
    calib_state = ESC_CALIBRATION_MIN;
    expect_next_status("ESC MIN");
    calib_state = ESC_CALIBRATION_DONE;
    expect_next_status("ESC DONE");
    calib_state = ESC_CALIBRATION_ABORT_MIN;
    expect_next_status("ESC ABORT");
    calib_state = ESC_CALIBRATION_PWM_ERROR;
    expect_next_status("ESC PWM ERR");

    reset();
    input.channels[7] = 1811;
    runtime_process();
    expect_next_status("FLASH N/A");
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

    /* With SB low, SC is a flight-mode request in every position. */
    const uint16_t positions[] = {172, 992, 1811};
    for (unsigned i = 0; i < 3; ++i) {
        reset();
        input.channels[5] = positions[i];
        runtime_process();
        assert(runtime_get_mode() == RUNTIME_FLIGHT && starts == 1);
        runtime_process();
        assert(runtime_get_command(&command));
        assert(command.mode == (app_command_switch_t)i);
        if (i == APP_COMMAND_SWITCH_MIDDLE) {
            continue;
        }
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
    /* SC middle + SB high takes priority over any SD position. */
    for (unsigned i = 0; i < 3U; ++i) {
        reset();
        input.channels[5] = 992;
        input.channels[6] = 1811;
        input.channels[7] = positions[i];
        runtime_process();
        assert(runtime_get_mode() == RUNTIME_IMU_CALIB &&
               runtime_mode_supported());
        assert(imu_calib_inits == 1 && imu_calib_polls == 0 &&
               calib_inits == 0);
        publish_update = false;
        runtime_process();
        assert(imu_calib_polls == 1 && !imu_calib_command.valid);
        publish_update = true;
        input.channels[7] = positions[(i + 1U) % 3U];
        runtime_process();
        assert(imu_calib_command.valid &&
               imu_calib_command.imu_calibration_requested);
        input.channels[5] = 172;
        runtime_process();
        assert(!imu_calib_command.imu_calibration_requested);
        assert(runtime_get_mode() == RUNTIME_IMU_CALIB && imu_calib_polls == 3);
        assert(starts == 0 && processes == 0 && stops == 1 && calib_inits == 0);
    }
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
    test_crsf_status();
    return 0;
}
