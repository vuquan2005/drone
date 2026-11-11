#include "SEGGER_RTT.h"
#include "app.h"
#include "app_imu.h"
#include "app_receiver.h"
#include "bsp_board.h"
#include "bsp_pwm.h"
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
    drain(1U, output);
    assert(strcmp(output, "IMU1,1,1001123,256,0,0,9806,0,0,0\n") == 0);
    tick++;
    app_process();
    assert(drain(0U, output) == 0U);
    drain(1U, output);
    assert(strcmp(output, "IMU1,2,1002123,256,0,0,9806,0,0,0\n") == 0);
    status = BSP_IMU_NO_DATA;
    app_process();
    assert(drain(1U, output) == 0U && pwm_starts == 1U);
    puts("App log/trace integration tests passed");
    return 0;
}
