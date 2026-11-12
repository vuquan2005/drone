#include "app_telemetry.h"

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

static bool put_milli(uint8_t *out, float value) {
    float scaled = value * 1000.0f;
    /* Reject NaN, infinities and out-of-range values before integer conversion.
     */
    if (!(scaled >= -2147483648.0f && scaled < 2147483648.0f)) {
        return false;
    }
    put_u32(out, (uint32_t)(int32_t)scaled);
    return true;
}

bsp_rtt_result_t app_telemetry_write_imu(const app_imu_sample_t *sample,
                                         uint32_t sequence) {
    if (sample == NULL || sample->sensor_time_ticks > 0xFFFFFFU) {
        return BSP_RTT_INVALID_ARGUMENT;
    }
    /* protocol/telemetry.md: explicit fields, never a native struct dump. */
    uint8_t frame[48] = {'D', 'R', 'T', '!', 1U, 1U, 36U, 0U};
    put_u32(frame + 8U, sequence);
    put_u32(frame + 12U, sample->t_read_us);
    put_u32(frame + 16U, sample->sensor_time_ticks);
    for (unsigned axis = 0; axis < 3U; ++axis) {
        if (!put_milli(frame + 20U + 4U * axis, sample->accel_mps2[axis]) ||
            !put_milli(frame + 32U + 4U * axis, sample->gyro_radps[axis])) {
            return BSP_RTT_INVALID_ARGUMENT;
        }
    }
    put_u32(frame + 44U, crc32(frame, 44U));
    return bsp_rtt_write(BSP_RTT_DATA, frame, sizeof(frame));
}
