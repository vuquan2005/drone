#include "app_telemetry.h"
#include <string.h>

enum { IMU_FRAME_BYTES = 44U, STATS_FRAME_BYTES = 52U };

static uint32_t stats_sequence;
static uint32_t receiver_sequence;
static uint32_t rate_sequence;
static uint32_t imu_full, stats_full, imu_rejected, stats_rejected;

void app_telemetry_init(void) {
    receiver_sequence = rate_sequence = 0U;
    stats_sequence = imu_full = stats_full = imu_rejected = stats_rejected = 0U;
}

static bsp_rtt_result_t track_imu(bsp_rtt_result_t result) {
    if (result == BSP_RTT_FULL) {
        imu_full++;
    } else if (result != BSP_RTT_OK) {
        imu_rejected++;
    }
    return result;
}

static bsp_rtt_result_t track_stats(bsp_rtt_result_t result) {
    if (result == BSP_RTT_FULL) {
        stats_full++;
    } else if (result != BSP_RTT_OK) {
        stats_rejected++;
    }
    return result;
}

static void put_u32(uint8_t *out, uint32_t value) {
    for (unsigned i = 0; i < 4U; ++i) {
        out[i] = (uint8_t)(value >> (8U * i));
    }
}

static uint32_t crc32(const uint8_t *data, size_t size) {
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8U; ++bit) {
            crc = (crc >> 1U) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
        }
    }
    return crc ^ UINT32_MAX;
}

static void put_u16(uint8_t *out, uint16_t value) {
    for (unsigned i = 0; i < 2U; ++i) {
        out[i] = (uint8_t)(value >> (8U * i));
    }
}

bsp_rtt_result_t app_telemetry_write_imu(const app_imu_sample_t *sample,
                                         uint32_t sequence) {
    if (sample == NULL || sample->sensor_time_ticks > 0xFFFFFFU) {
        return track_imu(BSP_RTT_INVALID_ARGUMENT);
    }
    bsp_rtt_result_t ready =
        bsp_rtt_prepare_write(BSP_RTT_DATA, IMU_FRAME_BYTES);
    if (ready != BSP_RTT_OK) {
        return track_imu(ready);
    }
    /* protocol/telemetry.md: explicit fields, never a native struct dump. */
    uint8_t frame[IMU_FRAME_BYTES] = {'D', 'R', 'T', '!', 1U, 4U, 32U, 0U};
    put_u32(frame + 8U, sequence);
    put_u32(frame + 12U, sample->t_read_us);
    put_u32(frame + 16U, sample->sensor_time_ticks);
    for (unsigned axis = 0; axis < 3U; ++axis) {
        put_u16(frame + 20U + 2U * axis, (uint16_t)sample->accel_raw[axis]);
        put_u16(frame + 26U + 2U * axis, (uint16_t)sample->gyro_raw[axis]);
    }
    frame[32U] = (uint8_t)BSP_IMU_ACCEL_RANGE_G;
    frame[33U] = 1U; /* Sensor XYZ -> body FLU: [X, -Y, -Z]. */
    put_u16(frame + 34U, (uint16_t)BSP_IMU_GYRO_RANGE_DPS);
    put_u16(frame + 36U, BSP_IMU_ODR_HZ);
    put_u16(frame + 38U, BSP_IMU_ODR_HZ);
    put_u32(frame + 40U, crc32(frame, 40U));
    return track_imu(bsp_rtt_write(BSP_RTT_DATA, frame, sizeof(frame)));
}

bsp_rtt_result_t app_telemetry_write_stats(uint32_t uptime_ms,
                                           uint32_t imu_samples,
                                           uint32_t imu_read_errors) {
    const uint32_t sequence = ++stats_sequence;
    bsp_rtt_result_t ready =
        bsp_rtt_prepare_write(BSP_RTT_DATA, STATS_FRAME_BYTES);
    if (ready != BSP_RTT_OK) {
        return track_stats(ready);
    }
    uint8_t frame[STATS_FRAME_BYTES] = {'D', 'R', 'T', '!', 1U, 2U, 40U, 0U};
    const uint32_t values[] = {sequence,
                               uptime_ms,
                               imu_samples,
                               imu_read_errors,
                               bsp_rtt_get_dropped_full(BSP_RTT_LOG),
                               bsp_rtt_get_dropped_full(BSP_RTT_DATA),
                               imu_full,
                               stats_full,
                               imu_rejected,
                               stats_rejected};
    for (unsigned i = 0; i < 10U; ++i) {
        put_u32(frame + 8U + i * 4U, values[i]);
    }
    put_u32(frame + 48U, crc32(frame, 48U));
    return track_stats(bsp_rtt_write(BSP_RTT_DATA, frame, sizeof(frame)));
}

bsp_rtt_result_t
app_telemetry_write_receiver(const app_receiver_sample_t *sample,
                             uint32_t uptime_ms) {
    const uint32_t sequence = ++receiver_sequence;
    if (sample == NULL) {
        return BSP_RTT_INVALID_ARGUMENT;
    }
    for (unsigned i = 0; i < CRSF_CHANNEL_COUNT; ++i) {
        if (sample->channels[i] > 2047U) {
            return BSP_RTT_INVALID_ARGUMENT;
        }
    }
    bsp_rtt_result_t ready = bsp_rtt_prepare_write(BSP_RTT_DATA, 60U);
    if (ready != BSP_RTT_OK) {
        return ready;
    }
    uint8_t frame[60] = {'D', 'R', 'T', '!', 1U, 3U, 48U, 0U};
    put_u32(frame + 8U, sequence);
    put_u32(frame + 12U, uptime_ms);
    put_u32(frame + 16U, sample->last_frame_ms);
    put_u32(frame + 20U, sample->connected ? 1U : 0U);
    for (unsigned i = 0; i < CRSF_CHANNEL_COUNT; ++i) {
        frame[24U + 2U * i] = (uint8_t)sample->channels[i];
        frame[25U + 2U * i] = (uint8_t)(sample->channels[i] >> 8U);
    }
    put_u32(frame + 56U, crc32(frame, 56U));
    return bsp_rtt_write(BSP_RTT_DATA, frame, sizeof(frame));
}

static void put_float(uint8_t *out, float value) {
    _Static_assert(sizeof(float) == sizeof(uint32_t),
                   "Wire floats require binary32");
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    put_u32(out, bits);
}

bsp_rtt_result_t app_telemetry_write_rate(const app_rate_t *rate) {
    const uint32_t sequence = ++rate_sequence;
    if (rate == NULL)
        return BSP_RTT_INVALID_ARGUMENT;
    bsp_rtt_result_t ready = bsp_rtt_prepare_write(BSP_RTT_DATA, 92U);
    if (ready != BSP_RTT_OK)
        return ready;
    uint8_t frame[92] = {'D', 'R', 'T', '!', 1U, 5U, 80U, 0U};
    put_u32(frame + 8, sequence);
    put_u32(frame + 12, rate->previous_us);
    put_u32(frame + 16, rate->dt_us);
    put_u32(frame + 20, (rate->configured ? 1U : 0U) | (rate->valid ? 2U : 0U));
    for (unsigned i = 0; i < 3; ++i) {
        put_float(frame + 24 + 4 * i, rate->setpoint_radps[i]);
        put_float(frame + 36 + 4 * i, rate->gyro_frd_radps[i]);
        put_float(frame + 48 + 4 * i, rate->output[i]);
        put_float(frame + 76 + 4 * i, rate->residual[i]);
    }
    for (unsigned i = 0; i < 4; ++i)
        put_float(frame + 60 + 4 * i,
                  (float)rate->mixer.motor[i] / NORM_Q15_ONE);
    put_u32(frame + 88, crc32(frame, 88));
    return bsp_rtt_write(BSP_RTT_DATA, frame, sizeof(frame));
}
