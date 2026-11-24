#include "accel_calibration.h"
#include "app.h"
#include "app_flash_journal.h"
#include "bsp_flash.h"
#include "bsp_motor_pwm.h"
#include "esc_calibration.h"
#include "flash_erase.h"
#include "imu_calibration.h"
#include "runtime.h"
#include <assert.h>
#include <stddef.h>
#include <string.h>

#if defined(DRONE_RUNTIME_FLASH_EXPORT)
#include "flash_export.h"

static unsigned export_inits, export_polls;
static unsigned boot_request_polls;
static bool boot_request;
void bsp_rtt_init(void) {}
void flash_export_init(void) {
    export_inits++;
}
bool flash_export_poll_boot_request(void) {
    boot_request_polls++;
    return boot_request;
}
void flash_export_process(uint32_t now_ms) {
    (void)now_ms;
    assert(export_inits == 1);
    export_polls++;
}
#endif

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
static imu_calibration_status_t imu_calib_status;
static app_status_t flight_status;
static esc_calibration_state_t calib_state;
static unsigned status_attempts;
static char status_label[CRSF_FLIGHT_MODE_TEXT_MAX + 1U];
static bool status_tx_ready;
static bool attitude_fresh;
static unsigned attitude_reads, attitude_attempts;
static enum { FLASH_IDLE, FLASH_PROBE, FLASH_CHIP_ERASE, FLASH_READ } flash_op;
static uint32_t *flash_id_out;
static uint8_t *flash_read_out;
static uint32_t flash_reads;
enum { MARKER_PAGES = BSP_FLASH_SECTOR_BYTES / BSP_FLASH_PAGE_BYTES };
static unsigned marker_reads, marker_writes;
static unsigned erase_calls;
static bool verify_dirty;

bsp_flash_result_t bsp_flash_probe_start(uint32_t *id) {
    assert(flash_op == FLASH_IDLE);
    flash_id_out = id;
    flash_op = FLASH_PROBE;
    return BSP_FLASH_PENDING;
}

bsp_flash_result_t bsp_flash_chip_erase_start(void) {
    assert(flash_op == FLASH_IDLE && *flash_id_out == BSP_FLASH_JEDEC_ID);
    ++erase_calls;
    flash_op = FLASH_CHIP_ERASE;
    return BSP_FLASH_PENDING;
}

bsp_flash_result_t bsp_flash_sector_erase_start(uint32_t address) {
    assert(flash_op == FLASH_IDLE && erase_calls == 0U);
    assert(address == BSP_FLASH_SIZE_BYTES - BSP_FLASH_SECTOR_BYTES);
    return BSP_FLASH_OK;
}

bsp_flash_result_t bsp_flash_program_start(uint32_t address, const void *data,
                                           size_t size) {
    assert(flash_op == FLASH_IDLE && erase_calls == 1U);
    assert(address == BSP_FLASH_SIZE_BYTES - BSP_FLASH_SECTOR_BYTES);
    assert(size == BSP_FLASH_PAGE_BYTES);
    assert(flash_reads == BSP_FLASH_SIZE_BYTES / BSP_FLASH_PAGE_BYTES);
    assert(app_flash_journal_is_marker(data, address / BSP_FLASH_PAGE_BYTES));
    ++marker_writes;
    return BSP_FLASH_OK;
}

bsp_flash_result_t bsp_flash_read_start(uint32_t address, void *out,
                                        size_t size) {
    assert(flash_op == FLASH_IDLE && address < BSP_FLASH_SIZE_BYTES &&
           size == BSP_FLASH_PAGE_BYTES);
    if (erase_calls == 0U) {
        assert(address == BSP_FLASH_SIZE_BYTES - BSP_FLASH_SECTOR_BYTES +
                              marker_reads * BSP_FLASH_PAGE_BYTES);
    } else {
        assert(address == flash_reads * BSP_FLASH_PAGE_BYTES);
    }
    flash_read_out = out;
    flash_op = FLASH_READ;
    return BSP_FLASH_PENDING;
}

bsp_flash_result_t bsp_flash_poll(void) {
    switch (flash_op) {
    case FLASH_PROBE:
        *flash_id_out = BSP_FLASH_JEDEC_ID;
        break;
    case FLASH_CHIP_ERASE:
        break;
    case FLASH_READ:
        memset(flash_read_out, 0xFF, BSP_FLASH_PAGE_BYTES);
        if (erase_calls == 0U) {
            ++marker_reads;
        } else {
            if (verify_dirty)
                flash_read_out[10] = 0U;
            ++flash_reads;
        }
        break;
    default:
        assert(false);
    }
    flash_op = FLASH_IDLE;
    return BSP_FLASH_OK;
}

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
imu_calibration_status_t imu_calibration_get_status(void) {
    assert(imu_calib_inits == 1 && imu_calib_polls != 0);
    return imu_calib_status;
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
    assert(strlen(label) < sizeof(status_label));
    strcpy(status_label, label);
    return true;
}
bool app_receiver_send_attitude(const float body_to_ned[4]) {
    assert(body_to_ned[0] == 1.0f && body_to_ned[1] == 0.0f &&
           body_to_ned[2] == 0.0f && body_to_ned[3] == 0.0f);
    attitude_attempts++;
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
    else {
        flight_status.arm_state = APP_FAILSAFE;
        flight_status.safety_reason = APP_SAFETY_RC;
    }
}
bool app_get_status(app_status_t *status) {
    assert(status != NULL && starts == 1 && processes != 0);
    *status = flight_status;
    return true;
}

bool app_get_attitude(float body_to_ned[4]) {
    assert(body_to_ned != NULL && starts == 1 && processes != 0);
    attitude_reads++;
    if (!attitude_fresh)
        return false;
    const float level[4] = {1.0f, 0.0f, 0.0f, 0.0f};
    memcpy(body_to_ned, level, sizeof(level));
    return true;
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
#if defined(DRONE_RUNTIME_FLASH_EXPORT)
    export_inits = export_polls = 0;
    boot_request_polls = 0;
    boot_request = false;
#endif
    flash_op = FLASH_IDLE;
    erase_calls = flash_reads = marker_reads = marker_writes = 0U;
    verify_dirty = false;
    imu_calib_status =
        (imu_calibration_status_t){.state = IMU_CALIBRATION_GYRO};
    flight_status =
        (app_status_t){.imu_initialized = true, .arm_state = APP_DISARMED};
    calib_state = ESC_CALIBRATION_READY;
    status_attempts = 0;
    status_label[0] = '\0';
    status_tx_ready = true;
    attitude_fresh = false;
    attitude_reads = attitude_attempts = 0;
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
    flight_status.arm_state = APP_ARMED;
    expect_next_status("ARMED");

    status_tx_ready = false;
    tick += 500U;
    runtime_process();
    assert(status_attempts == 4);
    status_tx_ready = true;
    runtime_process();
    assert(status_attempts == 4);
    input.connected = false;
    expect_next_status("FS RC");

    input.connected = true;
    flight_status.safety_reason = APP_SAFETY_IMU;
    expect_next_status("FS IMU");
    flight_status.imu_initialized = false;
    expect_next_status("IMU INIT");
    flight_status.safety_reason = APP_SAFETY_PWM;
    expect_next_status("FS PWM");
    flight_status.imu_initialized = true;
    flight_status.safety_reason = APP_SAFETY_CONTROL;
    expect_next_status("FS CTRL");
    flight_status.safety_reason = APP_SAFETY_CALIBRATION;
    expect_next_status("FAILSAFE");
    flight_status.arm_state = APP_DISARMED;
    flight_status.safety_reason = APP_SAFETY_IDLE_TIMEOUT;
    expect_next_status("IDLE TIMEOUT");
    flight_status.safety_reason = APP_SAFETY_OK;
    expect_next_status("DISARMED");

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
#if defined(DRONE_RUNTIME_FLASH_EXPORT)
    expect_next_status("EXPORT");
#else
    expect_next_status("WAIT SD");
#endif
}

static void test_crsf_imu_calibration(void) {
    reset();
    input.channels[5] = 992;
    input.channels[6] = 1811;
    attitude_fresh = true;
    runtime_process();
    assert(runtime_get_mode() == RUNTIME_IMU_CALIB);
    expect_next_status("IMU GYRO");

    imu_calib_status.state = IMU_CALIBRATION_ACCEL;
    expect_next_status("X+ 0/6");
    imu_calib_status.captured_faces = 1U << ACCEL_CAL_X_NEG;
    expect_next_status("X+ 1/6");
    imu_calib_status.captured_faces |=
        (1U << ACCEL_CAL_X_POS) | (1U << ACCEL_CAL_Y_POS);
    expect_next_status("Y- 3/6");
    imu_calib_status.captured_faces =
        ACCEL_CAL_ALL_FACES & ~(1U << ACCEL_CAL_Z_NEG);
    expect_next_status("Z- 5/6");

    const struct {
        imu_calibration_state_t state;
        const char *label;
    } results[] = {
        {IMU_CALIBRATION_DONE, "IMU DONE"},
        {IMU_CALIBRATION_RC_LOST, "IMU RC"},
        {IMU_CALIBRATION_CANCELLED, "IMU STOP"},
        {IMU_CALIBRATION_INIT_FAILED, "IMU INIT"},
        {IMU_CALIBRATION_READ_FAILED, "IMU READ"},
        {IMU_CALIBRATION_TIMEOUT, "IMU TIME"},
        {IMU_CALIBRATION_INVALID_ACCEL, "ACC ERR"},
        {IMU_CALIBRATION_SAVE_FAILED, "SAVE ERR"},
    };
    for (size_t i = 0; i < sizeof(results) / sizeof(results[0]); ++i) {
        imu_calib_status.state = results[i].state;
        expect_next_status(results[i].label);
    }
    assert(stops == 1 && starts == 0 && attitude_reads == 0U);
}

static void test_crsf_attitude(void) {
    tick = UINT32_MAX - 249U;
    reset();
    attitude_fresh = true;
    runtime_process();
    tick += 199U;
    runtime_process();
    assert(attitude_reads == 0U);
    tick++;
    runtime_process();
    assert(attitude_reads == 1U && attitude_attempts == 1U);

    tick += 299U;
    runtime_process(); /* t=499 ms, reserve the UART for FM. */
    assert(attitude_reads == 1U && status_attempts == 0U);
    tick++;
    runtime_process(); /* t=500 ms, both frames are due. */
    tick++;
    runtime_process();
    assert(attitude_reads == 1U && status_attempts == 1U);
    tick++;
    runtime_process();
    assert(attitude_reads == 2U && attitude_attempts == 2U);

    attitude_fresh = false;
    tick += 200U;
    runtime_process();
    assert(attitude_reads == 3U && attitude_attempts == 2U);
    attitude_fresh = true;
    runtime_process();
    assert(attitude_reads == 3U);
    tick += 200U;
    runtime_process();
    assert(attitude_reads == 4U && attitude_attempts == 3U);

    reset();
    attitude_fresh = true;
    input.channels[6] = 1811;
    runtime_process();
    tick += 250U;
    runtime_process();
    assert(runtime_get_mode() == RUNTIME_ESC_CALIB && attitude_reads == 0U);
}

static void select_manual_erase(void) {
    reset();
    input.channels[4] = input.channels[6] = input.channels[7] = 1811;
    input.channels[5] = 992; /* Erase takes priority over IMU calibration. */
    runtime_process();
    assert(runtime_get_mode() == RUNTIME_FLASH_ERASE &&
           runtime_mode_supported());
    assert(starts == 0 && calib_inits == 0 && imu_calib_inits == 0 &&
           stops == 1);
    expect_next_status("ERASE WAIT");
}

static void confirm_manual_erase(void) {
    input.channels[4] = 172;
    runtime_process();
    input.channels[4] = 1811;
    runtime_process();
    tick += 2999U;
    runtime_process();
    assert(flash_erase_get_state() == FLASH_ERASE_WAIT && erase_calls == 0U);
    ++tick;
    runtime_process();
    assert(flash_erase_get_state() == FLASH_ERASE_BUSY);
}

static void test_manual_erase(void) {
    select_manual_erase();
    tick += 4000U;
    runtime_process();
    assert(flash_erase_get_state() == FLASH_ERASE_WAIT && erase_calls == 0U);

    input.channels[4] = 992;
    runtime_process();
    input.channels[4] = 1811;
    tick += 4000U;
    runtime_process();
    assert(flash_erase_get_state() == FLASH_ERASE_WAIT && erase_calls == 0U);

    input.channels[4] = 172;
    runtime_process();
    input.channels[4] = 1811;
    runtime_process();
    tick += 2000U;
    input.connected = false;
    runtime_process();
    input.connected = true;
    tick += 4000U;
    runtime_process();
    assert(flash_erase_get_state() == FLASH_ERASE_WAIT && erase_calls == 0U);

    input.channels[4] = 172;
    runtime_process();
    input.channels[4] = input.channels[2] = 1811;
    tick += 4000U;
    runtime_process();
    input.channels[2] = 172;
    tick += 4000U;
    runtime_process();
    assert(flash_erase_get_state() == FLASH_ERASE_WAIT && erase_calls == 0U);

    confirm_manual_erase();
    expect_next_status("ERASING");
    input.connected = false;
    input.channels[4] = input.channels[6] = input.channels[7] = 172;
    for (unsigned i = 0;
         i < 2U * (BSP_FLASH_SIZE_BYTES / BSP_FLASH_PAGE_BYTES + MARKER_PAGES) +
                 12U;
         ++i) {
        runtime_process();
    }
    assert(flash_reads == BSP_FLASH_SIZE_BYTES / BSP_FLASH_PAGE_BYTES);
    assert(marker_reads == MARKER_PAGES && marker_writes == 1U);
    assert(erase_calls == 1U && stops == 1 && starts == 0 && processes == 0);
    assert(runtime_get_mode() == RUNTIME_FLASH_ERASE);
    expect_next_status("ERASE DONE");

    select_manual_erase();
    confirm_manual_erase();
    verify_dirty = true;
    for (unsigned i = 0; i < 2U * MARKER_PAGES + 10U; ++i) {
        runtime_process();
    }
    assert(erase_calls == 1U && flash_reads == 1U && marker_writes == 0U);
    expect_next_status("ERASE ERR");
}

int main(void) {
#if defined(DRONE_RUNTIME_FLASH_EXPORT)
    reset();
    input.connected = false;
    runtime_process();
    assert(runtime_get_mode() == RUNTIME_BOOT_SELECT && boot_request_polls == 1);
    boot_request = true;
    runtime_process();
    assert(runtime_get_mode() == RUNTIME_FLASH_EXPORT && boot_request_polls == 2);
    input.connected = true;
    runtime_process();
    assert(export_inits == 1 && export_polls == 1 && boot_request_polls == 2);
    assert(stops == 1 && starts == 0 && processes == 0 && erase_calls == 0);

    reset();
    boot_request = true;
    runtime_process();
    runtime_process();
    assert(runtime_get_mode() == RUNTIME_FLIGHT && boot_request_polls == 0);
    assert(starts == 1 && processes == 1 && export_polls == 0);
#endif
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
#if defined(DRONE_RUNTIME_FLASH_EXPORT)
    assert(runtime_get_mode() == RUNTIME_FLASH_EXPORT &&
           runtime_mode_supported());
    assert(export_inits == 1 && export_polls == 0);
    input.channels[7] = 172;
    input.connected = false;
    runtime_process();
    assert(runtime_get_mode() == RUNTIME_FLASH_EXPORT);
    assert(export_inits == 1 && export_polls == 1);
    assert(stops == 1 && starts == 0 && processes == 0 && erase_calls == 0);
#else
    assert(runtime_get_mode() == RUNTIME_BOOT_SELECT);
    assert(!runtime_mode_supported() && starts == 0 && processes == 0);
    input.channels[7] = 172;
    input.connected = false;
    runtime_process();
    assert(runtime_get_mode() == RUNTIME_BOOT_SELECT && stops == 1);
    input.channels[2] = input.channels[4] = 172;
    input.connected = true;
    runtime_process();
    assert(runtime_get_mode() == RUNTIME_FLIGHT && starts == 1);
#endif
    reset();
    input.channels[2] = input.channels[4] = input.channels[6] =
        input.channels[7] = 1811;
    input.connected = false;
    runtime_process();
    assert(runtime_get_mode() == RUNTIME_BOOT_SELECT && calib_inits == 0);
    input.connected = true;
    runtime_process();
    assert(runtime_get_mode() == RUNTIME_FLASH_ERASE && calib_inits == 0 &&
           starts == 0);
    reset();
    input.channels[7] = 992;
    runtime_process();
    assert(runtime_get_mode() == RUNTIME_FLIGHT);
    /* SC middle + SB high selects IMU calibration with SD OFF or RECORD. */
    for (unsigned i = 0; i < 2U; ++i) {
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
    test_crsf_imu_calibration();
    test_crsf_attitude();
    test_manual_erase();
    return 0;
}
