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
uint32_t HAL_GetTick(void) {
    return tick;
}
bool bsp_pwm_init(void) {
    pwm_starts++;
    return true;
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
        imu->sample.t_read_us = tick * 1000U + 123U;
        imu->sample.sensor_time_ticks = 256U;
        imu->sample.accel_mps2[2] = 9.80665f;
    }
    return status;
}
void app_receiver_init(void) {}
void app_receiver_poll(void) {}
static bool receiver_test_enabled;
bool app_receiver_get_sample(app_receiver_sample_t *sample) {
    if (!receiver_test_enabled) {
        return false;
    }
    memset(sample, 0, sizeof(*sample));
    sample->channels[0] = 172U;
    sample->channels[15] = 1811U;
    sample->last_frame_ms = tick - 10U;
    sample->connected = true;
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
    assert(drain(1U, output) == 48U);
    assert(memcmp(output, "DRT!\x01\x01\x24\x00", 8U) == 0);
    assert(get_u32(output + 8) == 1U && get_u32(output + 12) == 1001123U);
    assert(get_u32(output + 16) == 256U && get_u32(output + 28) == 9806U);
    tick++;
    app_process();
    assert(drain(0U, output) == 0U);
    assert(drain(1U, output) == 48U);
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
    receiver_test_enabled = true;
    tick = 548U;
    app_process();
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
    puts("App log/trace integration tests passed");
    return 0;
}
