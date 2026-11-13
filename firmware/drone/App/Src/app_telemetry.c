#include "app_telemetry.h"

enum { IMU_FRAME_BYTES = 48U, STATS_FRAME_BYTES = 52U };

static uint32_t stats_sequence;
static uint32_t receiver_sequence;
static uint32_t imu_full, stats_full, imu_rejected, stats_rejected;

void app_telemetry_init(void) {
    receiver_sequence = 0U;
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

static bool scale_milli(float value, int32_t *out) {
    float scaled = value * 1000.0f;
    /* Reject NaN, infinities and out-of-range values before integer conversion.
     */
    if (!(scaled >= -2147483648.0f && scaled < 2147483648.0f)) {
        return false;
    }
    *out = (int32_t)scaled;
    return true;
}

bsp_rtt_result_t app_telemetry_write_imu(const app_imu_sample_t *sample,
                                         uint32_t sequence) {
    if (sample == NULL || sample->sensor_time_ticks > 0xFFFFFFU) {
        return track_imu(BSP_RTT_INVALID_ARGUMENT);
    }
    /* Preserve invalid-payload precedence over FULL, even with a stalled host.
     */
    int32_t values[6];
    for (unsigned axis = 0; axis < 3U; ++axis) {
        if (!scale_milli(sample->accel_mps2[axis], &values[axis]) ||
            !scale_milli(sample->gyro_radps[axis], &values[axis + 3U])) {
            return track_imu(BSP_RTT_INVALID_ARGUMENT);
        }
    }
    bsp_rtt_result_t ready =
        bsp_rtt_prepare_write(BSP_RTT_DATA, IMU_FRAME_BYTES);
    if (ready != BSP_RTT_OK) {
        return track_imu(ready);
    }
    /* protocol/telemetry.md: explicit fields, never a native struct dump. */
    uint8_t frame[IMU_FRAME_BYTES] = {'D', 'R', 'T', '!', 1U, 1U, 36U, 0U};
    put_u32(frame + 8U, sequence);
    put_u32(frame + 12U, sample->t_read_us);
    put_u32(frame + 16U, sample->sensor_time_ticks);
    for (unsigned i = 0; i < 6U; ++i) {
        put_u32(frame + 20U + 4U * i, (uint32_t)values[i]);
    }
    put_u32(frame + 44U, crc32(frame, 44U));
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
