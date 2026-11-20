#include "app_telemetry.h"
#include "app_command.h"
#include "app_crc32.h"
#include <string.h>

enum { IMU_FRAME_BYTES = 44U, STATS_FRAME_BYTES = 52U, RC_FRAME_BYTES = 33U };

static uint32_t stats_sequence;
static uint32_t receiver_sequence;
static uint32_t rate_sequence;
static uint32_t attitude_sequence;
static uint32_t attitude_control_sequence;
static uint32_t imu_full, stats_full, imu_rejected, stats_rejected;
#if DRONE_IMU_TELEMETRY_DIVISOR > 1
static uint32_t flight_imu_sequence;
#endif

void app_telemetry_init(void) {
    receiver_sequence = rate_sequence = attitude_sequence = 0U;
    attitude_control_sequence = 0U;
    stats_sequence = imu_full = stats_full = imu_rejected = stats_rejected = 0U;
#if DRONE_IMU_TELEMETRY_DIVISOR > 1
    flight_imu_sequence = 0U;
#endif
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
    frame[33U] = APP_IMU_AXIS_MAP_ID;
    put_u16(frame + 34U, (uint16_t)BSP_IMU_GYRO_RANGE_DPS);
    put_u16(frame + 36U, BSP_IMU_ODR_HZ);
    put_u16(frame + 38U, BSP_IMU_ODR_HZ);
    put_u32(frame + 40U, app_crc32(frame, 40U));
    return track_imu(bsp_rtt_write(BSP_RTT_DATA, frame, sizeof(frame)));
}

#if DRONE_IMU_TELEMETRY_DIVISOR > 1
void app_telemetry_publish_flight_imu(const app_imu_sample_t *sample,
                                      uint32_t sample_count) {
    // A power-of-two divisor preserves the cadence across sample counter wrap.
    if ((sample_count - 1U) % DRONE_IMU_TELEMETRY_DIVISOR == 0U) {
        (void)app_telemetry_write_imu(sample, ++flight_imu_sequence);
    }
}
#endif

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
    put_u32(frame + 48U, app_crc32(frame, 48U));
    return track_stats(bsp_rtt_write(BSP_RTT_DATA, frame, sizeof(frame)));
}

bsp_rtt_result_t
app_telemetry_write_receiver(const app_receiver_sample_t *sample,
                             uint32_t uptime_ms) {
    const uint32_t sequence = ++receiver_sequence;
    if (sample == NULL) {
        return BSP_RTT_INVALID_ARGUMENT;
    }
    for (unsigned i = 0; i < 4U; ++i) {
        if (sample->channels[i] > 2047U) {
            return BSP_RTT_INVALID_ARGUMENT;
        }
    }
    uint8_t flags = 0U;
    if (sample->connected) {
        app_command_t command;
        if (!app_command_decode(sample, &command)) {
            return BSP_RTT_INVALID_ARGUMENT;
        }
        flags = 1U | (command.arm_requested ? 1U << 1U : 0U) |
                (command.esc_calibration_requested ? 1U << 2U : 0U) |
                ((uint8_t)command.mode << 3U);
        // SD wire codes follow its physical order LOW/MIDDLE/HIGH.
        if (command.telemetry_mode == APP_COMMAND_TELEMETRY_RECORD) {
            flags |= 1U << 5U;
        } else if (command.telemetry_mode == APP_COMMAND_TELEMETRY_STREAM) {
            flags |= 2U << 5U;
        }
    }
    bsp_rtt_result_t ready =
        bsp_rtt_prepare_write(BSP_RTT_DATA, RC_FRAME_BYTES);
    if (ready != BSP_RTT_OK) {
        return ready;
    }
    uint8_t frame[RC_FRAME_BYTES] = {'D', 'R', 'T', '!', 1U, 8U, 21U, 0U};
    put_u32(frame + 8U, sequence);
    put_u32(frame + 12U, uptime_ms);
    put_u32(frame + 16U, sample->last_frame_ms);
    for (unsigned i = 0; i < 4U; ++i) {
        put_u16(frame + 20U + 2U * i, sample->channels[i]);
    }
    frame[28U] = flags;
    put_u32(frame + 29U, app_crc32(frame, 29U));
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
    put_u32(frame + 88, app_crc32(frame, 88));
    return bsp_rtt_write(BSP_RTT_DATA, frame, sizeof(frame));
}

bsp_rtt_result_t app_telemetry_write_attitude(const float q[4],
                                              uint32_t t_sample_us) {
    const uint32_t sequence = ++attitude_sequence;
    if (q == NULL)
        return BSP_RTT_INVALID_ARGUMENT;
    uint8_t frame[36] = {'D', 'R', 'T', '!', 1U, 6U, 24U, 0U};
    const bsp_rtt_result_t ready =
        bsp_rtt_prepare_write(BSP_RTT_DATA, sizeof(frame));
    if (ready != BSP_RTT_OK)
        return ready;
    put_u32(frame + 8, sequence);
    put_u32(frame + 12, t_sample_us);
    for (unsigned i = 0; i < 4; ++i)
        put_float(frame + 16 + 4 * i, q[i]);
    put_u32(frame + 32, app_crc32(frame, 32));
    return bsp_rtt_write(BSP_RTT_DATA, frame, sizeof(frame));
}

bsp_rtt_result_t app_telemetry_write_attitude_control(
    const app_attitude_control_sample_t *sample) {
    const uint32_t sequence = ++attitude_control_sequence;
    if (sample == NULL)
        return BSP_RTT_INVALID_ARGUMENT;
    uint8_t frame[68] = {'D', 'R', 'T', '!', 1U, 7U, 56U, 0U};
    const bsp_rtt_result_t ready =
        bsp_rtt_prepare_write(BSP_RTT_DATA, sizeof(frame));
    if (ready != BSP_RTT_OK)
        return ready;
    put_u32(frame + 8, sequence);
    put_u32(frame + 12, sample->t_control_us);
    put_u32(frame + 16, sample->t_estimate_us);
    put_u32(frame + 20, sample->valid ? 1U : 0U);
    for (unsigned i = 0; i < 4U; ++i)
        put_float(frame + 24 + 4 * i, sample->body_to_ned[i]);
    for (unsigned i = 0; i < 2U; ++i) {
        put_float(frame + 40 + 4 * i, sample->target_rad[i]);
        put_float(frame + 48 + 4 * i,
                  sample->valid ? sample->tilt_error[i] : 0.0f);
        put_float(frame + 56 + 4 * i,
                  sample->valid ? sample->rate_radps[i] : 0.0f);
    }
    put_u32(frame + 64, app_crc32(frame, 64));
    return bsp_rtt_write(BSP_RTT_DATA, frame, sizeof(frame));
}
