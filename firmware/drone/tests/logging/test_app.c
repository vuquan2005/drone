#include "SEGGER_RTT.h"
#include "app.h"
#include "app_imu.h"
#include "app_log.h"
#include "app_receiver.h"
#include "app_telemetry.h"
#include "bsp_board.h"
#include "bsp_motor_pwm.h"
#include "bsp_rtt.h"
#include "imu_calibration.h"
#include "imu_calibration_store.h"
#include "runtime.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define IMU_SAMPLE_PERIOD_US (1000000U / BSP_IMU_ODR_HZ)
#define TEST_OUTPUT_SIZE 2048U

static bsp_imu_result_t status = BSP_IMU_NO_DATA;
static bsp_imu_result_t imu_init_status = BSP_IMU_OK;
static uint32_t tick = 1000U;
static uint32_t sub_tick_us;
static unsigned pwm_starts;
static uint16_t motor_pulse[4];
static bool pwm_write_ok = true;
static unsigned pwm_stops;
static float gyro_roll_radps;
static float accel_mps2[3] = {0.0f, 0.0f, -9.80665f};
static const app_imu_calibration_t *stored_calibration;
static const char *flight_mode_label;
static unsigned attitude_transmissions;

bool imu_calibration_load(uint8_t chip_id, app_imu_calibration_t *calibration) {
    assert(chip_id == 0x24U);
    if (stored_calibration == NULL) {
        return false;
    }
    *calibration = *stored_calibration;
    return true;
}
void imu_calibration_init(void) {
    assert(false);
}
void imu_calibration_process(const app_command_t *command) {
    (void)command;
    assert(false);
}
imu_calibration_status_t imu_calibration_get_status(void) {
    assert(false);
    return (imu_calibration_status_t){0};
}
app_config_t imu_calibration_get_config(void) {
    assert(false);
    return (app_config_t){0};
}
uint32_t bsp_time_now_us(void) {
    return tick * 1000U + 123U + sub_tick_us;
}
uint32_t HAL_GetTick(void) {
    return tick;
}
bool bsp_motor_pwm_init(void) {
    pwm_starts++;
    return true;
}
bool bsp_motor_pwm_set_all_us(
    const uint16_t pulse_us[BSP_MOTOR_PWM_MOTOR_COUNT]) {
    if (!pwm_write_ok)
        return false;
    memcpy(motor_pulse, pulse_us, sizeof(motor_pulse));
    return true;
}
void bsp_motor_pwm_stop(void) {
    pwm_stops++;
}
const bsp_imu_config_t *bsp_board_imu_config(void) {
    static const bsp_imu_config_t config = {.timeout_ms = 100U};
    return &config;
}
bsp_imu_result_t app_imu_init(app_imu_t *imu, const bsp_imu_config_t *config) {
    assert(config->timeout_ms == 100U);
    memset(imu, 0, sizeof(*imu));
    imu->status = imu_init_status;
    imu->sensor.initialized = imu_init_status == BSP_IMU_OK;
    imu->sensor.device.chip_id = 0x24;
    return imu->status;
}
bsp_imu_result_t app_imu_poll(app_imu_t *imu) {
    if (!imu->sensor.initialized)
        return imu->status;
    imu->status = status;
    if (status == BSP_IMU_DRIVER_ERROR) {
        imu->read_error_count++;
    }
    imu->sample_valid = status == BSP_IMU_OK;
    if (imu->sample_valid) {
        if (stored_calibration != NULL) {
            assert(imu->calibration.accel_valid ==
                   stored_calibration->accel_valid);
            for (unsigned axis = 0U; axis < 3U; ++axis) {
                assert(imu->calibration.gyro_bias_radps[axis] ==
                       stored_calibration->gyro_bias_radps[axis]);
                assert(imu->calibration.accel_bias_mps2[axis] ==
                       stored_calibration->accel_bias_mps2[axis]);
            }
            assert(memcmp(imu->calibration.accel_matrix,
                          stored_calibration->accel_matrix,
                          sizeof(imu->calibration.accel_matrix)) == 0);
        }
        imu->sample_count++;
        imu->sample.gyro_radps[0] = gyro_roll_radps;
        imu->sample.t_read_us = bsp_time_now_us();
        imu->sample.sensor_time_ticks = 256U;
        memcpy(imu->sample.accel_mps2, accel_mps2, sizeof(accel_mps2));
        imu->sample.accel_raw[2] = 2048;
    }
    return status;
}
void app_receiver_init(void) {}
void app_receiver_poll(void) {}
bool app_receiver_send_flight_mode(const char *label) {
    assert(label != NULL);
    flight_mode_label = label;
    return true;
}
bool app_receiver_send_attitude(const float body_to_ned[4]) {
    assert(body_to_ned != NULL);
    attitude_transmissions++;
    return true;
}
static bool receiver_test_enabled;
static bool receiver_connected = true;
static uint16_t receiver_roll = 172U;
static uint16_t receiver_throttle = 992U;
static uint16_t receiver_switches[4] = {992U, 992U, 992U, 992U};
bool app_receiver_get_sample(app_receiver_sample_t *sample) {
    if (!receiver_test_enabled) {
        return false;
    }
    memset(sample, 0, sizeof(*sample));
    for (unsigned i = 0; i < CRSF_CHANNEL_COUNT; ++i) {
        sample->channels[i] = 992U;
    }
    sample->channels[0] = receiver_roll;
    sample->channels[2] = receiver_throttle;
    for (unsigned i = 0; i < 4U; ++i) {
        sample->channels[i + 4U] = receiver_switches[i];
    }
    sample->channels[15] = 1811U;
    sample->last_frame_ms = tick - 10U;
    sample->connected = receiver_connected;
    return true;
}
bool app_receiver_update_sample(app_receiver_sample_t *sample,
                                uint32_t *sequence) {
    (*sequence)++;
    if (!app_receiver_get_sample(sample)) {
        *sample = (app_receiver_sample_t){0};
    }
    return true;
}
static app_command_t command;
static void init_app(void) {
    bsp_rtt_init();
    app_telemetry_init();
    app_log_init(HAL_GetTick);
    app_init();
}
/* Supply normalized input as runtime does; App performs no receiver I/O. */
static void process_app(void) {
    app_receiver_sample_t sample;
    app_command_decode(app_receiver_get_sample(&sample) ? &sample : NULL,
                       &command);
    app_process(&command);
}
static unsigned drain(unsigned channel, char *output) {
    SEGGER_RTT_BUFFER_UP *ring = &_SEGGER_RTT.aUp[channel];
    unsigned count = 0;
    while (ring->RdOff != ring->WrOff && count < TEST_OUTPUT_SIZE - 1U) {
        output[count++] = ring->pBuffer[ring->RdOff];
        ring->RdOff = (ring->RdOff + 1U) % ring->SizeOfBuffer;
    }
    output[count] = '\0';
    return count;
}
static uint32_t get_u32(const char *p) {
    return (uint32_t)(uint8_t)p[0] | ((uint32_t)(uint8_t)p[1] << 8U) |
           ((uint32_t)(uint8_t)p[2] << 16U) | ((uint32_t)(uint8_t)p[3] << 24U);
}

static void advance_time_us(uint32_t duration_us) {
    sub_tick_us += duration_us;
    tick += sub_tick_us / 1000U;
    sub_tick_us %= 1000U;
}

static void test_attitude_control(const app_rate_config_t *rate_config) {
    tick = 4294966U; /* MCU microsecond clock wraps during the first batch. */
    imu_init_status = BSP_IMU_OK;
    stored_calibration = NULL;
    init_app();
    status = BSP_IMU_OK;
    app_process(NULL);
    float q[4];
    assert(app_get_attitude(q) && q[1] == 0.0f);
    gyro_roll_radps = 2.0f;
    advance_time_us(IMU_SAMPLE_PERIOD_US);
    app_process(NULL);
    assert(app_get_attitude(q) && q[1] == 0.0f);
    app_process(NULL); /* A duplicate timestamp contributes no interval. */
    assert(app_get_attitude(q) && q[1] == 0.0f);
    gyro_roll_radps = 0.0f;
    for (unsigned interval = 1U; interval < 4U; ++interval) {
        advance_time_us(IMU_SAMPLE_PERIOD_US);
        app_process(NULL);
        assert(app_get_attitude(q));
        if (interval < 3U)
            assert(q[1] == 0.0f);
    }
    assert(fabsf(q[1] - 0.00125f) < 1e-6f);

    init_app();
    assert(app_configure_rate(rate_config));
    app_command_t pilot = {.valid = true,
                           .mode = APP_COMMAND_SWITCH_MIDDLE,
                           .throttle = NORM_Q15_ONE / 2};
    char trace[512];
    app_process(&pilot);
    assert(drain(1U, trace) ==
           68U); /* Only attitude control until the batch fills. */
    assert((uint8_t)trace[5] == 7U && get_u32(trace + 8U) == 1U);
    assert(get_u32(trace + 12U) == bsp_time_now_us());
    assert(get_u32(trace + 16U) == bsp_time_now_us());
    mixer_result_t result;
    pilot.roll = NORM_Q15_ONE;
    pilot.pitch = -NORM_Q15_ONE;
    pilot.yaw = NORM_Q15_ONE / 2;
    for (unsigned sample = 0U; sample < 7U; ++sample) {
        advance_time_us(IMU_SAMPLE_PERIOD_US);
        app_process(&pilot);
        assert(drain(1U, trace) == (sample == 6U ? 141U : 0U));
        assert(app_get_rate_result(&result));
        assert(result.achieved.roll == 0 && result.achieved.pitch == 0);
        assert(result.achieved.yaw > 3200 && result.achieved.yaw < 3350);
    }
    advance_time_us(IMU_SAMPLE_PERIOD_US);
    app_process(&pilot);
    assert(drain(1U, trace) == 68U);
    assert((uint8_t)trace[5] == 7U && get_u32(trace + 8U) == 2U);
    assert(app_get_rate_result(&result));
    assert(result.achieved.roll > 5000 && result.achieved.roll < 6000);
    assert(result.achieved.pitch < -5000 && result.achieved.pitch > -6000);
    pilot.mode = APP_COMMAND_SWITCH_LOW;
    advance_time_us(IMU_SAMPLE_PERIOD_US);
    app_process(&pilot);
    assert(drain(1U, trace) == 0U);
    assert(app_get_rate_result(&result) && result.achieved.roll > 6500);
    pilot.mode = APP_COMMAND_SWITCH_HIGH;
    pilot.roll = pilot.pitch = 0;
    advance_time_us(IMU_SAMPLE_PERIOD_US);
    app_process(&pilot);
    assert(app_get_rate_result(&result) && result.achieved.roll == 0 &&
           result.achieved.pitch == 0);

    /* Finish the current four-sample Mahony interval before testing staleness.
     */
    for (unsigned interval = 2U; interval < 4U; ++interval) {
        advance_time_us(IMU_SAMPLE_PERIOD_US);
        app_process(&pilot);
    }
    pilot.throttle = 0;
    pilot.arm_requested = true;
    status = BSP_IMU_NO_DATA;
    app_process(&pilot);
    assert(app_get_arm_state() == APP_ARMED);
    status = BSP_IMU_OK;
    tick += 19U;
    app_process(&pilot);
    assert(app_get_arm_state() == APP_ARMED);
    status = BSP_IMU_NO_DATA;
    tick += 2U;
    app_process(&pilot);
    app_status_t health;
    assert(app_get_status(&health) && health.imu_ready);
    assert(!app_get_attitude(q) && health.arm_state == APP_FAILSAFE);
    assert(!app_get_rate_result(&result));
    for (unsigned i = 0U; i < 4U; ++i)
        assert(motor_pulse[i] == 1000U);
    status = BSP_IMU_OK;
    app_process(&pilot); /* Reinitializes the estimate after the long gap. */
    assert(app_get_attitude(q));
    assert(app_get_arm_state() == APP_FAILSAFE);

    init_app();
    assert(app_configure_rate(rate_config));
    accel_mps2[1] = -2.538f;
    accel_mps2[2] = -9.472f; /* Approximately +15 degrees of body roll. */
    pilot.arm_requested = false;
    pilot.throttle = NORM_Q15_ONE / 2;
    pilot.yaw = 0;
    app_process(&pilot);
    tick += 2U;
    app_process(&pilot);
    assert(app_get_rate_result(&result));
    assert(result.achieved.roll < -3300 && result.achieved.roll > -3500);
    assert(result.achieved.pitch == 0 && result.achieved.yaw == 0);
}

int main(void) {
    init_app();
    char output[TEST_OUTPUT_SIZE];
    if (!APP_TELEMETRY_ENABLED) {
        drain(0U, output);
        assert(strstr(output,
                      "[I][rate] configured min_period_us=2000 frame=FRD\n") !=
               NULL);
        assert(
            strstr(output, "axis_config axis=roll kp_s_per_rad_x1e6=100000 ") !=
            NULL);
        assert(strstr(output, "d_filter_tau_us=0 max_rate_uradps=2000000\n") !=
               NULL);
        tick += 50U;
        status = BSP_IMU_OK;
        process_app();
        drain(0U, output);
        assert(
            strstr(
                output,
                "[I][attitude] estimate_state filter=mahony state=ready\n") !=
            NULL);
        tick += 50U;
        status = BSP_IMU_NO_DATA;
        process_app();
        drain(0U, output);
        assert(strstr(output, "[W][attitude] estimate_state filter=mahony "
                              "state=unavailable\n") != NULL);
        assert(drain(1U, output) == 0U);
        return 0;
    }
    app_status_t health;
    assert(!app_get_status(NULL));
    assert(app_get_status(&health) && !health.imu_ready && health.motor_ready);
    assert(health.imu_initialized && health.arm_state == APP_DISARMED);
    drain(0U, output);
    assert(strstr(output, "[1000][I][pwm] initialized\n") != NULL);
    assert(strstr(output, "[1000][I][imu] initialized id=0x24\n") != NULL);
    assert(strstr(output, "[I][attitude] filter_config name=mahony "
                          "nominal_hz=200 kp_per_s_x1e6=2000000 "
                          "ki_per_s2_x1e6=0\n") != NULL);
    assert(strstr(output,
                  "[I][attitude] controller_config nominal_hz=100 "
                  "kp_per_s_x1e6=4000000 max_tilt_urad=436332\n") != NULL);
    assert(strstr(output, "calibration_applied source=default frame=FRD ") !=
           NULL);
    process_app();
    assert(drain(0U, output) == 0U && drain(1U, output) == 0U);
    status = BSP_IMU_DRIVER_ERROR;
    for (unsigned i = 0; i < 20U; ++i) {
        process_app();
    }
    drain(0U, output);
    const char *failure = strstr(output, "read failed");
    assert(failure != NULL && strstr(failure + 1, "read failed") == NULL);
    assert(drain(1U, output) == 0U);
    status = BSP_IMU_OK;
    tick++;
    process_app();
    drain(0U, output);
    assert(strstr(output, "[1001][I][imu] read recovered\n") != NULL);
    assert(app_get_status(&health) && health.imu_ready);
    assert(drain(1U, output) == 0U);
    tick++;
    process_app();
    assert(drain(0U, output) == 0U);
    assert(drain(1U, output) == 0U);
    status = BSP_IMU_NO_DATA;
    process_app();
    assert(drain(1U, output) == 0U && pwm_starts == 1U);
    tick += 9U;
    app_telemetry_process_imu();
    assert(drain(1U, output) == 51U);
    assert(memcmp(output, "DRT!\x01\x09\x27\x00", 8U) == 0);
    assert(get_u32(output + 8) == 1U && get_u32(output + 12) == 1001123U);
    assert(get_u32(output + 16) == (2U << 24U) + 256U);
    assert((uint8_t)output[24] == 0U && (uint8_t)output[25] == 0x08U);
    assert(memcmp(output + 32U, "\xe8\x03\x00", 3U) ==
           0); /* 1 ms later, same sensor clock. */
    tick = 1999U;
    process_app();
    assert(drain(1U, output) == 0U);
    tick = 2000U;
    process_app();
    assert(drain(1U, output) == 52U);
    assert((uint8_t)output[5] == 2U);
    assert(get_u32(output + 8) == 1U && get_u32(output + 12) == 2000U);
    assert(get_u32(output + 16) == 2U && get_u32(output + 20) == 20U);
    char fill[4095];
    memset(fill, 'x', sizeof(fill));
    assert(bsp_rtt_write(BSP_RTT_DATA, fill, sizeof(fill)) == BSP_RTT_OK);
    tick = 3000U;
    process_app();
    for (unsigned i = 0; i < 100U; ++i) {
        process_app();
    }
    assert(bsp_rtt_get_dropped_full(BSP_RTT_DATA) == 1U);
    /* The data ring may hold more than the output buffer. */
    while (drain(1U, output) != 0U) {
    }
    tick = 4000U;
    process_app();
    assert(drain(1U, output) == 52U);
    assert(get_u32(output + 8) == 3U && get_u32(output + 28) == 1U);
    assert(get_u32(output + 36) == 1U);
    tick = 9000U;
    process_app();
    assert(drain(1U, output) == 52U); /* No catch-up burst after long delay. */
    tick = UINT32_MAX - 500U;
    init_app();
    tick = 499U;
    process_app();
    assert(drain(1U, output) == 52U); /* Scheduling survives HAL tick wrap. */
    drain(0U, output);
    receiver_test_enabled = true;
    tick = 548U;
    process_app();
    assert(command.valid);
    assert(command.roll == -NORM_Q15_ONE);
    assert(drain(0U, output) == 0U);
    assert(drain(1U, output) == 0U);
    tick = 549U;
    process_app();
    assert(drain(1U, output) == 0U); /* RC telemetry belongs to runtime. */
    process_app();
    assert(drain(1U, output) == 0U);
    /* Commands update with NO_DATA IMU and before the next telemetry slot. */
    receiver_roll = 1811U;
    tick++;
    process_app();
    assert(command.roll == NORM_Q15_ONE);
    assert(drain(0U, output) == 0U);
    receiver_switches[0] = 1811U;
    process_app();
    assert(drain(0U, output) > 0U);
    assert(strcmp(output, "[550][W][arm] rejected reason=imu_unhealthy\n") ==
           0);
    process_app();
    assert(drain(0U, output) == 0U);
    for (unsigned i = 1; i < 4U; ++i) {
        receiver_switches[i] = 1811U;
        process_app();
        assert(drain(0U, output) == 0U);
    }
    receiver_connected = false;
    process_app();
    assert(!command.valid);
    assert(command.roll == 0 && command.throttle == 0 &&
           !command.arm_requested);
    assert(drain(0U, output) == 0U);
    process_app();
    assert(drain(0U, output) == 0U);
    receiver_connected = true;
    process_app();
    assert(drain(0U, output) == 0U);
    init_app();
    assert(app_get_status(&health) && !health.imu_ready);
    receiver_switches[0] = receiver_switches[2] = 172U;
    receiver_switches[1] = 172U;
    mixer_result_t preview;
    assert(!app_get_rate_result(&preview));
    app_rate_config_t rate_config = {.period_us = 1000};
    for (unsigned i = 0; i < RATE_AXIS_COUNT; ++i) {
        rate_config.axis[i] = (rate_axis_config_t){0.1f, 0, 0, 0.5f, 1, 0};
        rate_config.max_rate_radps[i] = 2;
    }
    assert(app_configure_rate(&rate_config));
    status = BSP_IMU_OK;
    tick++;
    process_app();
    assert(!app_get_rate_result(&preview));
    tick++;
    process_app();
    assert(app_get_rate_result(&preview));
    assert(preview.achieved.roll > 0);
    status = BSP_IMU_NO_DATA;
    tick++;
    process_app();
    assert(app_get_rate_result(&preview));
    tick += 21;
    process_app();
    assert(!app_get_rate_result(&preview) && preview.motor[0] == 0);
    assert(app_get_status(&health) && !health.imu_ready && health.motor_ready);
    status = BSP_IMU_OK;
    tick++;
    process_app();
    tick++;
    process_app();
    assert(app_get_rate_result(&preview));
    receiver_connected = false;
    status = BSP_IMU_NO_DATA;
    process_app();
    assert(!app_get_rate_result(&preview));
    /* At 800 Hz, the 2 ms minimum makes PID consume every other sample. */
    rate_config.period_us = 2000;
    assert(app_configure_rate(&rate_config));
    receiver_connected = true;
    status = BSP_IMU_OK;
    tick++;
    process_app();
    advance_time_us(IMU_SAMPLE_PERIOD_US);
    gyro_roll_radps = 0.5f;
    process_app();
    assert(!app_get_rate_result(&preview));
    advance_time_us(IMU_SAMPLE_PERIOD_US);
    gyro_roll_radps = 1.0f;
    process_app();
    assert(app_get_rate_result(&preview));
    assert(preview.achieved.roll > 3200 && preview.achieved.roll < 3350);
    const norm_q15_t held_roll = preview.achieved.roll;
    advance_time_us(IMU_SAMPLE_PERIOD_US);
    gyro_roll_radps = 0;
    process_app();
    assert(app_get_rate_result(&preview) && preview.achieved.roll == held_roll);
    advance_time_us(IMU_SAMPLE_PERIOD_US);
    status = BSP_IMU_NO_DATA;
    process_app();
    assert(app_get_rate_result(&preview) && preview.achieved.roll == held_roll);
    status = BSP_IMU_OK;
    process_app();
    assert(app_get_rate_result(&preview) && preview.achieved.roll > held_roll);
    status = BSP_IMU_DRIVER_ERROR;
    process_app();
    assert(!app_get_rate_result(&preview));
    status = BSP_IMU_NO_DATA;
    tick++;
    process_app();
    assert(!app_get_rate_result(&preview));
    /* Real App -> motor -> PWM path: no startup-high arm, OFF/ON, RC loss. */
    receiver_connected = true;
    receiver_switches[2] = 172;
    receiver_switches[0] = 1811;
    receiver_throttle = 172;
    gyro_roll_radps = 0;
    rate_config.period_us = 1000;
    assert(app_configure_rate(&rate_config));
    status = BSP_IMU_OK;
    tick++;
    process_app();
    assert(app_get_arm_state() != APP_ARMED);
    drain(0U, output);
    assert(strstr(output, "[W][arm] rejected reason=arm_off_required\n") !=
           NULL);
    receiver_switches[0] = 172;
    tick++;
    process_app();
    receiver_switches[0] = 1811;
    receiver_throttle = 992;
    tick++;
    process_app();
    assert(app_get_arm_state() == APP_DISARMED);
    drain(0U, output);
    assert(strstr(output, "[W][arm] rejected reason=throttle_high\n") != NULL);
    receiver_throttle = 172;
    process_app();
    assert(app_get_arm_state() == APP_DISARMED);
    assert(drain(0U, output) == 0U);
    receiver_switches[0] = 172;
    process_app();
    receiver_switches[0] = 1811;
    tick++;
    process_app();
    assert(app_get_arm_state() == APP_ARMED);
    drain(0U, output);
    assert(strstr(output, "[I][arm] armed\n") != NULL);
    for (unsigned i = 0; i < 4; ++i)
        assert(motor_pulse[i] == 1000);
    receiver_throttle = 992;
    tick++;
    process_app();
    tick++;
    process_app();
    assert(app_get_arm_state() == APP_ARMED);
    assert(motor_pulse[0] > 1000 && motor_pulse[0] <= 2000);
    receiver_switches[0] = 172;
    status = BSP_IMU_NO_DATA;
    process_app();
    assert(app_get_arm_state() == APP_DISARMED);
    drain(0U, output);
    assert(strstr(output, "[I][arm] disarmed\n") != NULL);
    for (unsigned i = 0; i < 4; ++i)
        assert(motor_pulse[i] == 1000);
    receiver_throttle = 172;
    receiver_switches[0] = 1811;
    status = BSP_IMU_OK;
    tick++;
    process_app();
    assert(app_get_arm_state() == APP_ARMED);
    status = BSP_IMU_NO_DATA;
    tick += 21;
    process_app();
    assert(app_get_status(&health) && health.arm_state == APP_FAILSAFE);
    assert(health.imu_initialized && !health.imu_ready &&
           health.safety_reason == APP_SAFETY_IMU);
    drain(0U, output);
    assert(strstr(output, "[W][arm] failsafe reason=imu_unhealthy\n") != NULL);
    for (unsigned i = 0; i < 4; ++i)
        assert(motor_pulse[i] == 1000);
    receiver_switches[0] = 172;
    status = BSP_IMU_OK;
    tick++;
    process_app();
    receiver_switches[0] = 1811;
    tick++;
    process_app();
    assert(app_get_arm_state() == APP_ARMED);
    receiver_connected = false;
    status = BSP_IMU_NO_DATA;
    process_app();
    assert(app_get_arm_state() == APP_FAILSAFE);
    drain(0U, output);
    assert(strstr(output, "[W][arm] failsafe reason=rc_invalid\n") != NULL);
    for (unsigned i = 0; i < 4; ++i)
        assert(motor_pulse[i] == 1000);
    receiver_connected = true;
    receiver_throttle = 172;
    status = BSP_IMU_OK;
    tick++;
    process_app();
    assert(app_get_status(&health) && health.arm_state == APP_FAILSAFE);
    assert(health.safety_reason == APP_SAFETY_RC);
    receiver_switches[0] = 172;
    process_app();
    receiver_switches[0] = 1811;
    tick++;
    process_app();
    assert(app_get_arm_state() == APP_ARMED);
    status = BSP_IMU_DRIVER_ERROR;
    process_app();
    assert(app_get_arm_state() == APP_FAILSAFE);
    for (unsigned i = 0; i < 4; ++i)
        assert(motor_pulse[i] == 1000);
    pwm_write_ok = false;
    process_app();
    assert(pwm_stops == 1);
    drain(0U, output);
    assert(strstr(output, "[E][arm] failsafe reason=pwm_failure ") != NULL);
    pwm_write_ok = true;
    process_app();
    assert(app_get_arm_state() == APP_FAILSAFE && pwm_stops == 1);
    assert(app_get_status(&health) && !health.motor_ready && !health.imu_ready);
    assert(health.safety_reason == APP_SAFETY_PWM);
    /* Runtime owns shared transport, decodes RC and emits raw RC telemetry. */
    receiver_connected = false;
    receiver_throttle = receiver_switches[0] = 1811;
    receiver_switches[1] = receiver_switches[2] = receiver_switches[3] = 172;
    static const app_imu_calibration_t flash_calibration = {
        .gyro_bias_radps = {0.01f, -0.02f, 0.03f},
        .accel_bias_mps2 = {0.1f, 0.2f, -0.1f},
        .accel_matrix = {{1.01f, -0.03f, 0.0f},
                         {0.0f, 1.02f, 0.02f},
                         {0.04f, 0.0f, 0.98f}},
        .accel_valid = true,
    };
    stored_calibration = &flash_calibration;
    runtime_init();
    tick += 50U;
    runtime_process();
    assert(runtime_get_mode() == RUNTIME_BOOT_SELECT);
    drain(0U, output);
    assert(strstr(output, "[I][runtime] boot_select state=waiting_for_rc\n") !=
           NULL);
    assert(drain(1U, output) == 33U); /* RC visible before mode selection. */
    receiver_connected = true;
    runtime_process();
    assert(runtime_get_mode() == RUNTIME_FLIGHT && runtime_mode_supported());
    drain(0U, output);
    assert(strstr(output, "[I][runtime] selected mode=flight supported=1\n") !=
           NULL);
    assert(strstr(output,
                  "[I][imu] calibration_applied source=flash frame=FRD ") !=
           NULL);
    assert(strstr(output, "accel_enabled=1\n") != NULL);
    assert(strstr(output, "gyro_bias_uradps=10000,-20000,30000 ") != NULL);
    assert(strstr(output, "[I][imu] accel_correction frame=FRD "
                          "bias_umps2=100000,200000,-100000 "
                          "matrix_row_major_x1e6=1010000,-30000,0,0,1020000,"
                          "20000,40000,0,980000\n") != NULL);
    assert(app_configure_rate(&rate_config));
    tick += 50U;
    status = BSP_IMU_OK;
    runtime_process();
    assert(app_get_status(&health) && health.imu_ready && health.motor_ready);
    assert(health.arm_state == APP_DISARMED);
    for (unsigned i = 0; i < 4; ++i)
        assert(motor_pulse[i] == 1000);
    assert(runtime_get_command(&command) && command.valid);
    assert(drain(1U, output) >= 33U && (uint8_t)output[5] == 8U);
    assert((uint8_t)output[28U] == 0x03U);
    assert(get_u32(output + 12U) == tick);
    assert(get_u32(output + 16U) == tick - 10U);

    status = BSP_IMU_NO_DATA;
    tick += 10U;
    runtime_process();
    assert(drain(1U, output) == 36U && (uint8_t)output[5] == 9U);
    assert(SEGGER_RTT_WriteDownBuffer(0U, "C\x78\x56\x34\x12", 5U) == 5U);
    tick += 20U;
    runtime_process();
    assert(drain(1U, output) == 220U && (uint8_t)output[5] == 10U);
    assert(get_u32(output + 8U) == 0x12345678U);
    assert((uint8_t)output[12U] == 15U &&
           (uint8_t)output[13U] == APP_CONFIG_FLIGHT);
    assert(get_u32(output + 96U) == rate_config.period_us);
    status = BSP_IMU_OK;
    tick += 70U;
    runtime_process();
    assert(attitude_transmissions == 1U);
    status = BSP_IMU_NO_DATA;
    tick += 200U;
    runtime_process();
    assert(attitude_transmissions == 1U);
    app_process(NULL);
    assert(app_get_arm_state() == APP_FAILSAFE);

    imu_init_status = BSP_IMU_DRIVER_ERROR;
    receiver_switches[0] = 172;
    flight_mode_label = NULL;
    runtime_init();
    runtime_process();
    assert(runtime_get_mode() == RUNTIME_FLIGHT);
    tick += 500U;
    runtime_process();
    assert(app_get_status(&health) && health.arm_state == APP_FAILSAFE);
    assert(!health.imu_initialized && !health.imu_ready &&
           health.safety_reason == APP_SAFETY_IMU);
    assert(flight_mode_label && strcmp(flight_mode_label, "IMU INIT") == 0);

    test_attitude_control(&rate_config);
    puts("App log/trace integration tests passed");
    return 0;
}
