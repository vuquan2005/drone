#include "app_log.h"
#include "app_telemetry.h"
#include "bsp_time.h"
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
static uint32_t telemetry_us;
uint32_t bsp_time_now_us(void) {
    return telemetry_us;
}

static void expire_imu_batch(uint32_t first_read_us) {
    telemetry_us = first_read_us + 10000U;
    app_telemetry_process_imu();
}
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

static void test_config_response(void) {
    assert(SEGGER_RTT_WriteDownBuffer(0U, "!C\x78", 3U) == 3U);
    uint32_t request_id;
    assert(!app_telemetry_poll_config_request(&request_id));
    assert(SEGGER_RTT_WriteDownBuffer(0U, "\x56\x34\x12", 3U) == 3U);
    assert(app_telemetry_poll_config_request(&request_id));
    assert(request_id == 0x12345678U);
    assert(!app_telemetry_poll_config_request(&request_id));

    static const app_imu_t imu = {
        .sensor = {.device.chip_id = 0xd1U,
                   .initialized = true,
                   .settings = {11, 11, 12, 0, 2, 2, 0x11, 0x15, 255, 255, 255,
                                255}},
        .calibration = {.gyro_bias_radps = {0.125f, -0.25f, 0.5f},
                        .accel_bias_mps2 = {-1.0f, 2.0f, -3.0f},
                        .accel_matrix = {{1.0f, 0.125f, 0.0f},
                                         {0.0f, 0.875f, 0.0f},
                                         {0.0f, 0.0f, 1.25f}},
                        .accel_valid = true},
    };
    static const app_rate_config_t rate = {
        .axis = {{0.25f, 0.125f, 0.0625f, 0.5f, 1.0f, 0.015625f},
                 {0.5f, 0.25f, 0.125f, 0.75f, 1.0f, 0.03125f},
                 {1.0f, 0.5f, 0.25f, 0.25f, 0.5f, 0.0625f}},
        .max_rate_radps = {2.0f, 3.0f, 4.0f},
        .period_us = 2000U,
    };
    const app_config_t config = {
        .mode = APP_CONFIG_FLIGHT,
        .imu = &imu,
        .calibration_from_flash = true,
        .rate = &rate,
        .imu_timeout_us = 20000U,
        .mahony_hz = 200U,
        .attitude_hz = 100U,
        .mahony_kp_per_s = 2.0f,
        .attitude_kp_per_s = 4.0f,
        .attitude_max_rad = 0.5f,
        .gravity_min_mps2 = 7.85f,
        .gravity_max_mps2 = 11.77f,
    };
    char output[221];
    assert(app_telemetry_write_config(request_id, &config) == BSP_RTT_OK);
    assert(drain(BSP_RTT_DATA, output, sizeof(output)) == 220U);
    check_fixture(output, 220U, CONFIG_FIXTURE_PATH);
}

static void enabled_tests(void) {
    bsp_rtt_init();
    app_telemetry_init();
    test_config_response();
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

    app_imu_sample_t sample = {.t_read_us = 1000000U,
                               .sensor_time_ticks = 0x123456U,
                               .accel_raw = {-120, 35, 2048},
                               .gyro_raw = {10, -20, 30},
                               .accel_mps2 = {NAN, INFINITY, -INFINITY}};
    app_telemetry_queue_imu(&sample, 1000U);
    sample = (app_imu_sample_t){.t_read_us = 1001250U,
                                .sensor_time_ticks = 0x123476U,
                                .accel_raw = {-118, 37, 2050},
                                .gyro_raw = {11, -18, 31}};
    app_telemetry_queue_imu(&sample, 1001U);
    telemetry_us = 1009999U;
    app_telemetry_process_imu();
    assert(drain(BSP_RTT_DATA, output, sizeof(output)) == 0U);
    expire_imu_batch(1000000U);
    assert(drain(BSP_RTT_DATA, output, sizeof(output)) == 51U);
    check_fixture(output, 51U, IMU_BATCH_FIXTURE_PATH);

    const uint32_t first_read_us = UINT32_MAX - 500U;
    for (uint32_t i = 0U; i < 8U; ++i) {
        sample.t_read_us = first_read_us + 1250U * i;
        sample.sensor_time_ticks = (0xfffff0U + 32U * i) & 0xffffffU;
        sample.accel_raw[0] = INT16_MIN;
        sample.gyro_raw[2] = INT16_MAX;
        app_telemetry_queue_imu(&sample, UINT32_MAX - 1U + i);
    }
    assert(drain(BSP_RTT_DATA, output, sizeof(output)) == 141U);
    assert(memcmp(output, "DRT!\x01\x09\x81\x00", 8U) == 0);
    assert(get_u32(output + 8U) == UINT32_MAX - 1U);
    assert(get_u32(output + 12U) == first_read_us);
    assert((uint8_t)output[19U] == 8U);
    assert(memcmp(output + 32U, "\xe2\x04\x20", 3U) == 0);
    assert((uint8_t)output[20U] == 0U && (uint8_t)output[21U] == 0x80U);
    assert((uint8_t)output[30U] == 0xffU && (uint8_t)output[31U] == 0x7fU);

    app_telemetry_queue_imu(&sample, 17U);
    sample.t_read_us += 1U;
    app_telemetry_queue_imu(&sample, 19U); /* Gap closes the first batch. */
    assert(drain(BSP_RTT_DATA, output, sizeof(output)) == 36U);
    assert(get_u32(output + 8U) == 17U);
    sample.t_read_us += 1U;
    sample.sensor_time_ticks += UINT8_MAX;
    app_telemetry_queue_imu(&sample, 20U);
    assert(drain(BSP_RTT_DATA, output, sizeof(output)) == 0U);
    sample.t_read_us += 1U;
    sample.sensor_time_ticks += UINT8_MAX + 1U;
    app_telemetry_queue_imu(
        &sample, 21U); /* Delta 256 needs a new base before the deadline. */
    assert(drain(BSP_RTT_DATA, output, sizeof(output)) == 51U);
    assert(get_u32(output + 8U) == 19U);
    assert((uint8_t)output[34U] == UINT8_MAX);
    expire_imu_batch(sample.t_read_us);
    assert(drain(BSP_RTT_DATA, output, sizeof(output)) == 36U);
    assert(get_u32(output + 8U) == 21U);

    char fill[4095];
    memset(fill, 'x', sizeof(fill));
    bsp_rtt_init();
    app_telemetry_init();
    assert(app_telemetry_write_stats(1234U, 17U, 3U) == BSP_RTT_OK);
    assert(drain(BSP_RTT_DATA, output, sizeof(output)) == 52U);
    check_fixture(output, 52U, RTT_STATS_FIXTURE_PATH);
    assert(bsp_rtt_write(BSP_RTT_DATA, fill, sizeof(fill)) == BSP_RTT_OK);
    for (uint32_t i = 0U; i < 8U; ++i) {
        sample.t_read_us += 1250U;
        app_telemetry_queue_imu(&sample, 18U + i);
    }
    assert(app_telemetry_write_stats(2234U, 25U, 3U) == BSP_RTT_FULL);
    assert(bsp_rtt_get_dropped_full(BSP_RTT_DATA) == 2U);
    drain(BSP_RTT_DATA, output, sizeof(output));
    expire_imu_batch(sample.t_read_us);
    assert(drain(BSP_RTT_DATA, output, sizeof(output)) == 0U); /* No retry. */
    const uint32_t sensor_ticks = sample.sensor_time_ticks;
    sample.sensor_time_ticks = 0x1000000U;
    app_telemetry_queue_imu(&sample, 26U);
    sample.sensor_time_ticks = sensor_ticks;
    assert(app_telemetry_write_stats(3234U, 26U, 3U) == BSP_RTT_OK);
    assert(drain(BSP_RTT_DATA, output, sizeof(output)) == 52U);
    assert(get_u32(output + 8U) == 3U);  /* Stats attempt 2 was dropped. */
    assert(get_u32(output + 28U) == 2U); /* All DATA frames dropped-full. */
    assert(get_u32(output + 32U) == 1U); /* One full IMU batch. */
    assert(get_u32(output + 36U) == 1U); /* Stats full only. */
    assert(get_u32(output + 40U) == 1U); /* Invalid IMU, not a full drop. */
    bsp_rtt_init();
    app_telemetry_init();
    app_receiver_sample_t rc = {
        .channels = {172U, 992U, 1811U, 2047U, 1811U, 992U, 1811U, 1811U},
        .last_frame_ms = 1224U,
        .connected = true};
    assert(app_telemetry_write_receiver(&rc, 1234U) == BSP_RTT_OK);
    assert(drain(BSP_RTT_DATA, output, sizeof(output)) == 33U);
    check_fixture(output, 33U, RC_FIXTURE_PATH);
    assert(app_telemetry_write_receiver(NULL, 1234U) ==
           BSP_RTT_INVALID_ARGUMENT);
    rc.channels[7] = 2048U;
    assert(app_telemetry_write_receiver(&rc, 1234U) ==
           BSP_RTT_INVALID_ARGUMENT);
    rc.channels[7] = 992U;
    assert(bsp_rtt_write(BSP_RTT_DATA, fill, sizeof(fill)) == BSP_RTT_OK);
    assert(app_telemetry_write_receiver(&rc, 1234U) == BSP_RTT_FULL);
    drain(BSP_RTT_DATA, output, sizeof(output));
    assert(app_telemetry_write_receiver(&rc, 1284U) == BSP_RTT_OK);
    assert(drain(BSP_RTT_DATA, output, sizeof(output)) == 33U);
    assert((uint8_t)output[28U] == 0x2fU); /* SD MIDDLE encodes RECORD. */
    rc.connected = false;
    assert(app_telemetry_write_receiver(&rc, 1334U) == BSP_RTT_OK);
    assert(drain(BSP_RTT_DATA, output, sizeof(output)) == 33U);
    assert(get_u32(output + 8U) == 6U);
    assert((uint8_t)output[28U] == 0U);
    assert((uint8_t)output[20U] == 172U); /* Keep old channels on disconnect. */
    bsp_rtt_init();
    app_telemetry_init();
    app_rate_t rate = {.configured = true,
                       .valid = true,
                       .previous_us = 123456,
                       .dt_us = 2000,
                       .setpoint_radps = {0.25f, -0.5f, 1},
                       .gyro_frd_radps = {0.1f, -0.2f, 0.3f},
                       .output = {0.015f, -0.03f, 0.07f},
                       .residual = {0, 0, 0.01f}};
    /* Exact Q15 fractions on the wire. */
    for (unsigned i = 0; i < 4; ++i)
        rate.mixer.motor[i] = (norm_q15_t)(8192 * (i + 1));
    assert(app_telemetry_write_rate(&rate) == BSP_RTT_OK);
    assert(drain(BSP_RTT_DATA, output, sizeof(output)) == 92U);
    check_fixture(output, 92U, RATE_FIXTURE_PATH);
    assert(app_telemetry_write_rate(NULL) == BSP_RTT_INVALID_ARGUMENT);
    assert(bsp_rtt_write(BSP_RTT_DATA, fill, sizeof(fill)) == BSP_RTT_OK);
    assert(app_telemetry_write_rate(&rate) == BSP_RTT_FULL);
    bsp_rtt_init();
    const float q[4] = {0.8f, 0.6f, 0.0f, 0.0f};
    assert(app_telemetry_write_attitude(q, 123456U) == BSP_RTT_OK);
    assert(drain(BSP_RTT_DATA, output, sizeof(output)) == 36U);
    check_fixture(output, 36U, ATTITUDE_FIXTURE_PATH);
    app_attitude_control_sample_t evaluation = {
        .t_control_us = 123500U,
        .t_estimate_us = 123456U,
        .body_to_ned = {1.0f},
        .target_rad = {0.25f, -0.125f},
        .tilt_error = {0.125f, -0.0625f},
        .rate_radps = {0.5f, -0.25f},
        .valid = true};
    assert(app_telemetry_write_attitude_control(&evaluation) == BSP_RTT_OK);
    assert(drain(BSP_RTT_DATA, output, sizeof(output)) == 68U);
    check_fixture(output, 68U, ATTITUDE_CONTROL_FIXTURE_PATH);
    assert(bsp_rtt_write(BSP_RTT_DATA, fill, sizeof(fill)) == BSP_RTT_OK);
    assert(app_telemetry_write_attitude_control(&evaluation) == BSP_RTT_FULL);
    bsp_rtt_init();
    evaluation.valid = false;
    assert(app_telemetry_write_attitude_control(&evaluation) == BSP_RTT_OK);
    assert(drain(BSP_RTT_DATA, output, sizeof(output)) == 68U);
    assert(get_u32(output + 8U) == 3U && get_u32(output + 20U) == 0U);
    for (unsigned offset = 48U; offset < 64U; offset += 4U) {
        assert(get_u32(output + offset) == 0U);
    }
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
    APP_LOG_ERROR("imu", "%d", ++effects);
    APP_LOG_WARN("imu", "%d", ++effects);
    APP_LOG_INFO("imu", "%d", ++effects);
    APP_LOG_DEBUG("imu", "%d", ++effects);
    app_telemetry_queue_imu(NULL, 0U);
    app_telemetry_process_imu();
    assert(effects == 0 && clock_calls == 0U);
#endif
    puts("logging tests passed");
    return 0;
}
