#include "SEGGER_RTT.h"
#include "app.h"
#include "app_imu.h"
#include "app_receiver.h"
#include "bsp_board.h"
#include "bsp_pwm.h"
#include "bsp_rtt.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static bsp_imu_result_t status = BSP_IMU_NO_DATA;
static uint32_t tick = 1000U;
static unsigned pwm_starts;
static uint16_t motor_pulse[4];
static bool pwm_write_ok = true;
static unsigned pwm_stops;
static float gyro_roll_radps;
uint32_t bsp_time_now_us(void) {
    return tick * 1000U + 123U;
}
uint32_t HAL_GetTick(void) {
    return tick;
}
bool bsp_pwm_init(void) {
    pwm_starts++;
    return true;
}
bool bsp_pwm_set_all_us(const uint16_t pulse_us[BSP_PWM_CHANNEL_COUNT]) {
    if (!pwm_write_ok)
        return false;
    memcpy(motor_pulse, pulse_us, sizeof(motor_pulse));
    return true;
}
void bsp_pwm_stop(void) {
    pwm_stops++;
}
const bsp_bmi270_config_t *bsp_board_imu_config(void) {
    static const bsp_bmi270_config_t config = {.timeout_ms = 100U};
    return &config;
}
bsp_imu_result_t app_imu_init(app_imu_t *imu,
                              const bsp_bmi270_config_t *config) {
    assert(config->timeout_ms == 100U);
    memset(imu, 0, sizeof(*imu));
    imu->status = BSP_IMU_OK;
    imu->sensor.device.chip_id = 0x24;
    return BSP_IMU_OK;
}
bsp_imu_result_t app_imu_poll(app_imu_t *imu) {
    imu->status = status;
    if (status == BSP_IMU_DRIVER_ERROR) {
        imu->read_error_count++;
    }
    imu->sample_valid = status == BSP_IMU_OK;
    if (imu->sample_valid) {
        imu->sample_count++;
        imu->sample.gyro_radps[0] = gyro_roll_radps;
        imu->sample.t_read_us = tick * 1000U + 123U;
        imu->sample.sensor_time_ticks = 256U;
        imu->sample.accel_mps2[2] = 9.80665f;
        imu->sample.accel_raw[2] = -2048;
    }
    return status;
}
void app_receiver_init(void) {}
void app_receiver_poll(void) {}
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
static unsigned drain(unsigned channel, char *output) {
    SEGGER_RTT_BUFFER_UP *ring = &_SEGGER_RTT.aUp[channel];
    unsigned count = 0;
    while (ring->RdOff != ring->WrOff && count < 511U) {
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
int main(void) {
    app_init();
    app_command_t command;
    assert(!app_get_command(NULL));
    assert(app_get_command(&command) && !command.valid);
    char output[512];
    drain(0U, output);
    assert(strstr(output, "[1000][I][pwm] initialized\n") != NULL);
    assert(strstr(output, "[1000][I][imu] initialized id=0x24\n") != NULL);
    app_process();
    assert(drain(0U, output) == 0U && drain(1U, output) == 0U);
    status = BSP_IMU_DRIVER_ERROR;
    for (unsigned i = 0; i < 20U; ++i) {
        app_process();
    }
    drain(0U, output);
    const char *failure = strstr(output, "read failed");
    assert(failure != NULL && strstr(failure + 1, "read failed") == NULL);
    assert(drain(1U, output) == 0U);
    status = BSP_IMU_OK;
    tick++;
    app_process();
    drain(0U, output);
    assert(strstr(output, "[1001][I][imu] read recovered\n") != NULL);
    assert(drain(1U, output) == 44U);
    assert(memcmp(output, "DRT!\x01\x04\x20\x00", 8U) == 0);
    assert(get_u32(output + 8) == 1U && get_u32(output + 12) == 1001123U);
    assert(get_u32(output + 16) == 256U);
    assert((uint8_t)output[24] == 0U && (uint8_t)output[25] == 0xF8U);
    tick++;
    app_process();
    assert(drain(0U, output) == 0U);
    assert(drain(1U, output) == 44U);
    assert(get_u32(output + 8) == 2U && get_u32(output + 12) == 1002123U);
    status = BSP_IMU_NO_DATA;
    app_process();
    assert(drain(1U, output) == 0U && pwm_starts == 1U);
    tick = 1999U;
    app_process();
    assert(drain(1U, output) == 0U);
    tick = 2000U;
    app_process();
    assert(drain(1U, output) == 52U);
    assert((uint8_t)output[5] == 2U);
    assert(get_u32(output + 8) == 1U && get_u32(output + 12) == 2000U);
    assert(get_u32(output + 16) == 2U && get_u32(output + 20) == 20U);
    char fill[4095];
    memset(fill, 'x', sizeof(fill));
    assert(bsp_rtt_write(BSP_RTT_DATA, fill, sizeof(fill)) == BSP_RTT_OK);
    tick = 3000U;
    app_process();
    for (unsigned i = 0; i < 100U; ++i) {
        app_process();
    }
    assert(bsp_rtt_get_dropped_full(BSP_RTT_DATA) == 1U);
    /* Drain all bytes (the small output buffer can only hold 511 at a time). */
    while (drain(1U, output) != 0U) {
    }
    tick = 4000U;
    app_process();
    assert(drain(1U, output) == 52U);
    assert(get_u32(output + 8) == 3U && get_u32(output + 28) == 1U);
    assert(get_u32(output + 36) == 1U);
    tick = 9000U;
    app_process();
    assert(drain(1U, output) == 52U); /* No catch-up burst after long delay. */
    tick = UINT32_MAX - 500U;
    app_init();
    tick = 499U;
    app_process();
    assert(drain(1U, output) == 52U); /* Scheduling survives HAL tick wrap. */
    drain(0U, output);
    receiver_test_enabled = true;
    tick = 548U;
    app_process();
    assert(app_get_command(&command) && command.valid);
    assert(command.roll == -NORM_Q15_ONE);
    assert(drain(0U, output) > 0U);
    assert(strstr(output,
                  "[I][command] valid=1 arm_requested=0 mode=middle "
                  "esc_calibration_requested=0 telemetry=record\n") != NULL);
    assert(drain(1U, output) == 0U);
    tick = 549U;
    app_process();
    assert(drain(1U, output) == 60U);
    assert((uint8_t)output[5] == 3U);
    assert(get_u32(output + 8U) == 1U);
    assert(get_u32(output + 12U) == 549U);
    assert(get_u32(output + 16U) == 539U);
    assert(get_u32(output + 20U) == 1U);
    app_process();
    assert(drain(1U, output) == 0U);
    /* Commands update with NO_DATA IMU and before the next telemetry slot. */
    receiver_roll = 1811U;
    tick++;
    app_process();
    assert(app_get_command(&command) && command.roll == NORM_Q15_ONE);
    assert(drain(0U, output) == 0U);
    const char *expected[] = {"arm_requested=1", "mode=high",
                              "esc_calibration_requested=1",
                              "telemetry=stream"};
    for (unsigned i = 0; i < 4U; ++i) {
        receiver_switches[i] = 1811U;
        app_process();
        assert(drain(0U, output) > 0U);
        assert(strstr(output, expected[i]) != NULL);
        assert(strstr(output, "roll=") == NULL &&
               strstr(output, "pitch=") == NULL);
        assert(strstr(output, "yaw=") == NULL &&
               strstr(output, "throttle=") == NULL);
        app_process();
        assert(drain(0U, output) == 0U);
    }
    receiver_connected = false;
    app_process();
    assert(app_get_command(&command) && !command.valid);
    assert(command.roll == 0 && command.throttle == 0 &&
           !command.arm_requested);
    assert(drain(0U, output) > 0U);
    assert(strstr(output, "[I][command] valid=0\n") != NULL);
    app_process();
    assert(drain(0U, output) == 0U);
    receiver_connected = true;
    app_process();
    assert(drain(0U, output) > 0U);
    assert(strstr(output, "valid=1 arm_requested=1") != NULL);
    app_init();
    assert(app_get_command(&command) && !command.valid);
    receiver_switches[0] = receiver_switches[2] = 172U;
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
    app_process();
    assert(!app_get_rate_result(&preview));
    tick++;
    app_process();
    assert(app_get_rate_result(&preview));
    assert(preview.achieved.roll > 0);
    status = BSP_IMU_NO_DATA;
    tick++;
    app_process();
    assert(app_get_rate_result(&preview));
    tick += 21;
    app_process();
    assert(!app_get_rate_result(&preview) && preview.motor[0] == 0);
    status = BSP_IMU_OK;
    tick++;
    app_process();
    tick++;
    app_process();
    assert(app_get_rate_result(&preview));
    receiver_connected = false;
    status = BSP_IMU_NO_DATA;
    app_process();
    assert(!app_get_rate_result(&preview));
    /* IMU at 1 kHz, rate at 500 Hz: intermediate samples do not run PID. */
    rate_config.period_us = 2000;
    assert(app_configure_rate(&rate_config));
    receiver_connected = true;
    status = BSP_IMU_OK;
    tick++;
    app_process();
    tick++;
    gyro_roll_radps = 0.5f;
    app_process();
    assert(!app_get_rate_result(&preview));
    tick++;
    gyro_roll_radps = 1.0f;
    app_process();
    assert(app_get_rate_result(&preview));
    assert(preview.achieved.roll > 3200 && preview.achieved.roll < 3350);
    const norm_q15_t held_roll = preview.achieved.roll;
    tick++;
    gyro_roll_radps = 0;
    app_process();
    assert(app_get_rate_result(&preview) && preview.achieved.roll == held_roll);
    tick++;
    status = BSP_IMU_NO_DATA;
    app_process();
    assert(app_get_rate_result(&preview) && preview.achieved.roll == held_roll);
    status = BSP_IMU_OK;
    app_process();
    assert(app_get_rate_result(&preview) && preview.achieved.roll > held_roll);
    status = BSP_IMU_DRIVER_ERROR;
    app_process();
    assert(!app_get_rate_result(&preview));
    status = BSP_IMU_NO_DATA;
    tick++;
    app_process();
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
    app_process();
    assert(app_get_arm_state() != APP_ARMED);
    receiver_switches[0] = 172;
    tick++;
    app_process();
    receiver_switches[0] = 1811;
    tick++;
    app_process();
    assert(app_get_arm_state() == APP_ARMED);
    for (unsigned i = 0; i < 4; ++i)
        assert(motor_pulse[i] == 1000);
    receiver_throttle = 992;
    tick++;
    app_process();
    tick++;
    app_process();
    assert(app_get_arm_state() == APP_ARMED);
    assert(motor_pulse[0] > 1000 && motor_pulse[0] <= 2000);
    receiver_switches[0] = 172;
    status = BSP_IMU_NO_DATA;
    app_process();
    assert(app_get_arm_state() == APP_DISARMED);
    for (unsigned i = 0; i < 4; ++i)
        assert(motor_pulse[i] == 1000);
    receiver_throttle = 172;
    receiver_switches[0] = 1811;
    status = BSP_IMU_OK;
    tick++;
    app_process();
    assert(app_get_arm_state() == APP_ARMED);
    status = BSP_IMU_NO_DATA;
    tick += 21;
    app_process();
    assert(app_get_arm_state() == APP_FAILSAFE);
    for (unsigned i = 0; i < 4; ++i)
        assert(motor_pulse[i] == 1000);
    receiver_switches[0] = 172;
    status = BSP_IMU_OK;
    tick++;
    app_process();
    receiver_switches[0] = 1811;
    tick++;
    app_process();
    assert(app_get_arm_state() == APP_ARMED);
    receiver_connected = false;
    status = BSP_IMU_NO_DATA;
    app_process();
    assert(app_get_arm_state() == APP_FAILSAFE);
    for (unsigned i = 0; i < 4; ++i)
        assert(motor_pulse[i] == 1000);
    receiver_connected = true;
    receiver_throttle = 172;
    status = BSP_IMU_OK;
    tick++;
    app_process();
    assert(app_get_arm_state() == APP_FAILSAFE);
    receiver_switches[0] = 172;
    app_process();
    receiver_switches[0] = 1811;
    tick++;
    app_process();
    assert(app_get_arm_state() == APP_ARMED);
    status = BSP_IMU_DRIVER_ERROR;
    app_process();
    assert(app_get_arm_state() == APP_FAILSAFE);
    for (unsigned i = 0; i < 4; ++i)
        assert(motor_pulse[i] == 1000);
    pwm_write_ok = false;
    app_process();
    assert(pwm_stops == 1);
    pwm_write_ok = true;
    app_process();
    assert(app_get_arm_state() == APP_FAILSAFE && pwm_stops == 1);
    puts("App log/trace integration tests passed");
    return 0;
}
