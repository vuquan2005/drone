#include "app_log.h"
#include "app_telemetry.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static uint32_t tick_ms = 1234U;
static unsigned clock_calls;
static uint32_t clock_ms(void) {
    clock_calls++;
    return tick_ms;
}

#if DRONE_RTT
#include "SEGGER_RTT.h"
static unsigned drain(unsigned channel, char *output, unsigned capacity) {
    SEGGER_RTT_BUFFER_UP *ring = &_SEGGER_RTT.aUp[channel];
    unsigned count = 0;
    while (ring->RdOff != ring->WrOff && count + 1U < capacity) {
        output[count++] = ring->pBuffer[ring->RdOff];
        ring->RdOff = (ring->RdOff + 1U) % ring->SizeOfBuffer;
    }
    output[count] = '\0';
    return count;
}

static void check_fixture(const char *output, unsigned size, const char *path) {
    FILE *fixture = fopen(path, "r");
    assert(fixture != NULL);
    for (unsigned i = 0; i < size; ++i) {
        unsigned byte;
        assert(fscanf(fixture, "%2x", &byte) == 1);
        assert((uint8_t)output[i] == byte);
    }
    unsigned extra;
    assert(fscanf(fixture, "%x", &extra) == EOF);
    fclose(fixture);
}
static uint32_t get_u32(const char *p) {
    return (uint32_t)(uint8_t)p[0] | ((uint32_t)(uint8_t)p[1] << 8U) |
           ((uint32_t)(uint8_t)p[2] << 16U) | ((uint32_t)(uint8_t)p[3] << 24U);
}
static void enabled_tests(void) {
    bsp_rtt_init();
    app_telemetry_init();
    assert(app_log_write(APP_LOG_LEVEL_INFO, "imu", "boot") ==
           BSP_RTT_NOT_INITIALIZED);
    app_log_init(clock_ms);
    char output[4097];
    APP_LOG_INFO("imu.spi", "initialized id=0x%02x", 0x24U);
    drain(BSP_RTT_LOG, output, sizeof(output));
    assert(strcmp(output, "[1234][I][imu.spi] initialized id=0x24\n") == 0);
    APP_LOG_WARN("receiver", "line1\nline2\r");
    drain(BSP_RTT_LOG, output, sizeof(output));
    assert(strcmp(output, "[1234][W][receiver] line1 line2 \n") == 0);
    APP_LOG_ERROR("imu", "failed=%d", -5);
    drain(BSP_RTT_LOG, output, sizeof(output));
    assert(strcmp(output, "[1234][E][imu] failed=-5\n") == 0);
    assert(app_log_write(99U, "imu", "bad") == BSP_RTT_INVALID_ARGUMENT);
    assert(app_log_write(0U, "imu", "bad") == BSP_RTT_INVALID_ARGUMENT);
    assert(app_log_write(3U, "", "bad") == BSP_RTT_INVALID_ARGUMENT);
    assert(app_log_write(3U, "bad tag", "bad") == BSP_RTT_INVALID_ARGUMENT);
    assert(app_log_write(3U, "01234567890123456", "bad") ==
           BSP_RTT_INVALID_ARGUMENT);
    assert(bsp_rtt_get_dropped_full(BSP_RTT_LOG) == 0U);
    int side_effect = 0;
    unsigned before = clock_calls;
    APP_LOG_DEBUG("imu", "detail=%d", ++side_effect);
#if DRONE_LOG_LEVEL >= APP_LOG_LEVEL_DEBUG
    assert(side_effect == 1 && clock_calls == before + 1U);
    drain(BSP_RTT_LOG, output, sizeof(output));
    assert(strstr(output, "[D][imu] detail=1\n") != NULL);
#else
    assert(side_effect == 0 && clock_calls == before);
    assert(drain(BSP_RTT_LOG, output, sizeof(output)) == 0U);
    assert(app_log_write(4U, "imu", "filtered") == BSP_RTT_OK);
    assert(clock_calls == before);
#endif
    char long_message[600];
    memset(long_message, 'x', sizeof(long_message) - 1U);
    long_message[sizeof(long_message) - 1U] = '\0';
    APP_LOG_INFO("imu", "%s", long_message);
    unsigned length = drain(BSP_RTT_LOG, output, sizeof(output));
    assert(length == 255U);
    const char suffix[] = "...[truncated]\n";
    assert(strcmp(output + length - (sizeof(suffix) - 1U), suffix) == 0);
    tick_ms = UINT32_MAX;
    APP_LOG_INFO("imu", "before wrap");
    drain(BSP_RTT_LOG, output, sizeof(output));
    assert(strncmp(output, "[4294967295]", 12U) == 0);
    tick_ms = 0U;
    APP_LOG_INFO("imu", "after wrap");
    drain(BSP_RTT_LOG, output, sizeof(output));
    assert(strncmp(output, "[0]", 3U) == 0);
    bsp_rtt_result_t result;
    unsigned queued = 0;
    while ((result = app_log_write(3U, "imu", "fill")) == BSP_RTT_OK) {
        assert(++queued < 200U);
    }
    assert(result == BSP_RTT_FULL &&
           bsp_rtt_get_dropped_full(BSP_RTT_LOG) == 1U);

    app_imu_sample_t sample = {.t_read_us = 123456U,
                               .sensor_time_ticks = 0x123456U,
                               .accel_mps2 = {1.25f, -2.5f, 9.80665f},
                               .gyro_radps = {1.0f, -2.0f, 3.0f}};
    app_imu_sample_t original = sample;
    before = clock_calls;
    assert(app_telemetry_write_imu(NULL, 17U) == BSP_RTT_INVALID_ARGUMENT);
    assert(app_telemetry_write_imu(&sample, 17U) == BSP_RTT_OK);
    assert(drain(BSP_RTT_DATA, output, sizeof(output)) == 48U);
    check_fixture(output, 48U, TELEMETRY_FIXTURE_PATH);
    assert(clock_calls == before &&
           memcmp(&sample, &original, sizeof(sample)) == 0);
    char fill[4095];
    memset(fill, 'x', sizeof(fill));
    assert(bsp_rtt_write(BSP_RTT_DATA, fill, sizeof(fill)) == BSP_RTT_OK);
    sample.accel_mps2[0] = NAN;
    assert(app_telemetry_write_imu(&sample, 18U) == BSP_RTT_INVALID_ARGUMENT);
    assert(bsp_rtt_get_dropped_full(BSP_RTT_DATA) == 0U);
    sample = original;
    assert(app_telemetry_write_imu(&sample, 18U) == BSP_RTT_FULL);
    assert(bsp_rtt_get_dropped_full(BSP_RTT_DATA) == 1U);
    assert(memcmp(&sample, &original, sizeof(sample)) == 0);
    drain(BSP_RTT_DATA, output, sizeof(output));
    assert(app_telemetry_write_imu(&sample, 19U) == BSP_RTT_OK);
    drain(BSP_RTT_DATA, output, sizeof(output));
    assert(memcmp(output, "DRT!\x01\x01\x24\x00", 8U) == 0);
    assert((uint8_t)output[8] == 19U);
    const float invalid_values[] = {NAN, INFINITY, -INFINITY, 3e10f, -3e10f};
    for (unsigned i = 0; i < sizeof(invalid_values) / sizeof(invalid_values[0]);
         ++i) {
        sample.accel_mps2[0] = invalid_values[i];
        assert(app_telemetry_write_imu(&sample, 20U) ==
               BSP_RTT_INVALID_ARGUMENT);
        assert(drain(BSP_RTT_DATA, output, sizeof(output)) == 0U);
    }
    sample = original;
    sample.sensor_time_ticks = 0x1000000U;
    assert(app_telemetry_write_imu(&sample, 20U) == BSP_RTT_INVALID_ARGUMENT);
    assert(bsp_rtt_get_dropped_full(BSP_RTT_DATA) == 1U);
    bsp_rtt_init();
    app_telemetry_init();
    assert(app_telemetry_write_stats(1234U, 17U, 3U) == BSP_RTT_OK);
    assert(drain(BSP_RTT_DATA, output, sizeof(output)) == 52U);
    check_fixture(output, 52U, RTT_STATS_FIXTURE_PATH);
    assert(bsp_rtt_write(BSP_RTT_DATA, fill, sizeof(fill)) == BSP_RTT_OK);
    sample = original;
    assert(app_telemetry_write_imu(&sample, 18U) == BSP_RTT_FULL);
    assert(app_telemetry_write_stats(2234U, 18U, 3U) == BSP_RTT_FULL);
    assert(bsp_rtt_get_dropped_full(BSP_RTT_DATA) == 2U);
    drain(BSP_RTT_DATA, output, sizeof(output));
    assert(app_telemetry_write_imu(NULL, 19U) == BSP_RTT_INVALID_ARGUMENT);
    assert(app_telemetry_write_stats(3234U, 19U, 3U) == BSP_RTT_OK);
    assert(drain(BSP_RTT_DATA, output, sizeof(output)) == 52U);
    assert(get_u32(output + 8U) == 3U);  /* Stats attempt 2 was dropped. */
    assert(get_u32(output + 28U) == 2U); /* All DATA frames dropped-full. */
    assert(get_u32(output + 32U) == 1U); /* IMU full only. */
    assert(get_u32(output + 36U) == 1U); /* Stats full only. */
    assert(get_u32(output + 40U) == 1U); /* Invalid IMU, not a full drop. */
    bsp_rtt_init();
    app_telemetry_init();
    app_receiver_sample_t rc = {.channels = {172U, 992U, 1811U, 2047U},
                                .last_frame_ms = 1224U,
                                .connected = true};
    assert(app_telemetry_write_receiver(&rc, 1234U) == BSP_RTT_OK);
    assert(drain(BSP_RTT_DATA, output, sizeof(output)) == 60U);
    check_fixture(output, 60U, RC_FIXTURE_PATH);
    assert(app_telemetry_write_receiver(NULL, 1234U) ==
           BSP_RTT_INVALID_ARGUMENT);
    rc.channels[15] = 2048U;
    assert(app_telemetry_write_receiver(&rc, 1234U) ==
           BSP_RTT_INVALID_ARGUMENT);
    rc.channels[15] = 0U;
    assert(bsp_rtt_write(BSP_RTT_DATA, fill, sizeof(fill)) == BSP_RTT_OK);
    assert(app_telemetry_write_receiver(&rc, 1234U) == BSP_RTT_FULL);
    drain(BSP_RTT_DATA, output, sizeof(output));
    rc.connected = false;
    assert(app_telemetry_write_receiver(&rc, 1334U) == BSP_RTT_OK);
    assert(drain(BSP_RTT_DATA, output, sizeof(output)) == 60U);
    assert(get_u32(output + 8U) == 5U);
    assert(get_u32(output + 20U) == 0U);
    assert((uint8_t)output[24U] == 172U); /* Keep old channels on disconnect. */
    app_log_init(NULL);
    assert(app_log_write(1U, "imu", "no clock") == BSP_RTT_NOT_INITIALIZED);
}
#endif

int main(void) {
#if DRONE_RTT
    enabled_tests();
#else
    int effects = 0;
    app_log_init(clock_ms);
    app_telemetry_init();
    assert(bsp_rtt_prepare_write(BSP_RTT_DATA, 48U) == BSP_RTT_DISABLED);
    APP_TELEMETRY_STATS(++effects, ++effects, ++effects);
    APP_LOG_ERROR("imu", "%d", ++effects);
    APP_LOG_WARN("imu", "%d", ++effects);
    APP_LOG_INFO("imu", "%d", ++effects);
    APP_LOG_DEBUG("imu", "%d", ++effects);
    APP_TELEMETRY_IMU(NULL, ++effects);
    APP_TELEMETRY_RECEIVER(NULL, ++effects);
    assert(effects == 0 && clock_calls == 0U);
#endif
    puts("logging tests passed");
    return 0;
}
