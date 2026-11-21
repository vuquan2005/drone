#include "app_telemetry.h"
#include "app_command.h"
#include "app_crc32.h"
#include "bsp_time.h"
#include <string.h>

enum {
    STATS_FRAME_BYTES = 52U,
    RC_FRAME_BYTES = 33U,
    RATE_FRAME_BYTES = 66U,
    ATTITUDE_CONTROL_FRAME_BYTES = 68U,
    ATTITUDE_CONTROL_COMPACT_FRAME_BYTES = 62U,
    ATTITUDE_CONTROL_AGE_MASK = 0x7FFFU,
    ATTITUDE_CONTROL_VALID_BIT = 0x8000U,
    CONFIG_FRAME_BYTES = 8U + 4U + APP_CONFIG_PAYLOAD_BYTES + 4U,
    GET_CONFIG = 'C',
    IMU_BATCH_CAPACITY = 8U,
    IMU_BATCH_MAX_AGE_US = 10000U,
    IMU_FIRST_AXES_OFFSET = 20U,
    IMU_NEXT_SAMPLE_OFFSET = 32U,
    IMU_NEXT_SAMPLE_BYTES = 15U,
    IMU_MAX_FRAME_BYTES = IMU_NEXT_SAMPLE_OFFSET +
                          (IMU_BATCH_CAPACITY - 1U) * IMU_NEXT_SAMPLE_BYTES +
                          4U,
    IMU_SENSOR_TICK_MASK = 0xFFFFFFU,
};

_Static_assert(BSP_IMU_ACCEL_RANGE_G == 16 && BSP_IMU_GYRO_RANGE_DPS == 2000 &&
                   APP_IMU_AXIS_MAP_ID == 2 && BSP_IMU_ODR_HZ == 800,
               "RTT IMU type 9 requires the fixed sensor configuration");

static struct {
    uint8_t frame[IMU_MAX_FRAME_BYTES];
    uint32_t first_read_us;
    uint32_t last_read_us;
    uint32_t last_sensor_ticks;
    uint32_t last_sequence;
    uint8_t count;
} imu_batch;

static uint32_t stats_sequence;
static uint32_t receiver_sequence;
static uint32_t rate_sequence;
static uint32_t attitude_sequence;
static uint32_t attitude_control_sequence;
static uint32_t imu_full, stats_full, imu_rejected, stats_rejected;
static uint32_t config_request_id;
static unsigned config_request_bytes;

void app_telemetry_init(void) {
    receiver_sequence = rate_sequence = attitude_sequence = 0U;
    attitude_control_sequence = 0U;
    stats_sequence = imu_full = stats_full = imu_rejected = stats_rejected = 0U;
    imu_batch.count = 0U;
    config_request_bytes = 0U;
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

bool app_telemetry_poll_config_request(uint32_t *id) {
    uint8_t byte;
    for (unsigned i = 0U; i < 15U && bsp_rtt_read_control(&byte, 1U); ++i) {
        if (config_request_bytes == 0U) {
            if (byte == GET_CONFIG) {
                config_request_id = 0U;
                config_request_bytes = 1U;
            }
        } else {
            config_request_id |= (uint32_t)byte
                                 << (8U * (config_request_bytes - 1U));
            if (++config_request_bytes == 5U) {
                config_request_bytes = 0U;
                *id = config_request_id;
                return true;
            }
        }
    }
    return false;
}

bsp_rtt_result_t app_telemetry_write_config(uint32_t id,
                                            const app_config_t *config) {
    const bsp_rtt_result_t ready =
        bsp_rtt_prepare_write(BSP_RTT_DATA, CONFIG_FRAME_BYTES);
    if (ready != BSP_RTT_OK)
        return ready;
    uint8_t frame[CONFIG_FRAME_BYTES] = {'D', 'R', 'T', '!', 1U, 10U};
    put_u16(frame + 6U, CONFIG_FRAME_BYTES - 12U);
    put_u32(frame + 8U, id);
    app_config_encode(frame + 12U, config);
    const size_t crc_offset = CONFIG_FRAME_BYTES - 4U;
    put_u32(frame + crc_offset, app_crc32(frame, crc_offset));
    return bsp_rtt_write(BSP_RTT_DATA, frame, sizeof(frame));
}

static void flush_imu_batch(void) {
    const size_t crc_offset =
        IMU_NEXT_SAMPLE_OFFSET + (imu_batch.count - 1U) * IMU_NEXT_SAMPLE_BYTES;
    const size_t frame_bytes = crc_offset + 4U;
    bsp_rtt_result_t result = bsp_rtt_prepare_write(BSP_RTT_DATA, frame_bytes);
    if (result == BSP_RTT_OK) {
        static const uint8_t header[] = {'D', 'R', 'T', '!', 1U, 9U};
        uint8_t *frame = imu_batch.frame;
        memcpy(frame, header, sizeof(header));
        put_u16(frame + 6U, (uint16_t)(crc_offset - 8U));
        frame[19U] = imu_batch.count;
        put_u32(frame + crc_offset, app_crc32(frame, crc_offset));
        result = bsp_rtt_write(BSP_RTT_DATA, frame, frame_bytes);
    }
    if (result == BSP_RTT_FULL) {
        imu_full++;
    } else if (result != BSP_RTT_OK) {
        imu_rejected++;
    }
    imu_batch.count = 0U;
}

void app_telemetry_queue_imu(const app_imu_sample_t *sample,
                             uint32_t sequence) {
    if (sample == NULL || sample->sensor_time_ticks > IMU_SENSOR_TICK_MASK) {
        imu_rejected++;
        return;
    }
    const uint32_t read_delta_us = sample->t_read_us - imu_batch.last_read_us;
    const uint32_t sensor_delta_ticks =
        (sample->sensor_time_ticks - imu_batch.last_sensor_ticks) &
        IMU_SENSOR_TICK_MASK;
    if (imu_batch.count != 0U &&
        (sequence != imu_batch.last_sequence + 1U ||
         read_delta_us > UINT16_MAX || sensor_delta_ticks > UINT8_MAX ||
         sample->t_read_us - imu_batch.first_read_us >= IMU_BATCH_MAX_AGE_US)) {
        flush_imu_batch();
    }

    uint8_t *axes;
    if (imu_batch.count == 0U) {
        uint8_t *frame = imu_batch.frame;
        put_u32(frame + 8U, sequence);
        put_u32(frame + 12U, sample->t_read_us);
        put_u16(frame + 16U, (uint16_t)sample->sensor_time_ticks);
        frame[18U] = (uint8_t)(sample->sensor_time_ticks >> 16U);
        imu_batch.first_read_us = sample->t_read_us;
        axes = frame + IMU_FIRST_AXES_OFFSET;
    } else {
        uint8_t *record = imu_batch.frame + IMU_NEXT_SAMPLE_OFFSET +
                          (imu_batch.count - 1U) * IMU_NEXT_SAMPLE_BYTES;
        put_u16(record, (uint16_t)read_delta_us);
        record[2U] = (uint8_t)sensor_delta_ticks;
        axes = record + 3U;
    }
    for (unsigned axis = 0; axis < 3U; ++axis) {
        put_u16(axes + 2U * axis, (uint16_t)sample->accel_raw[axis]);
        put_u16(axes + 6U + 2U * axis, (uint16_t)sample->gyro_raw[axis]);
    }
    imu_batch.last_read_us = sample->t_read_us;
    imu_batch.last_sensor_ticks = sample->sensor_time_ticks;
    imu_batch.last_sequence = sequence;
    if (++imu_batch.count == IMU_BATCH_CAPACITY) {
        flush_imu_batch();
    }
}

void app_telemetry_process_imu(void) {
    if (imu_batch.count != 0U &&
        bsp_time_now_us() - imu_batch.first_read_us >= IMU_BATCH_MAX_AGE_US) {
        flush_imu_batch();
    }
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
    _Static_assert(NORM_Q15_ONE == 32768 && RATE_AXIS_COUNT == 3U &&
                       MIXER_MOTOR_COUNT == 4U,
                   "Rate type 11 requires the Quad X Q15 layout");
    const uint32_t sequence = ++rate_sequence;
    if (rate == NULL || !rate->configured)
        return BSP_RTT_INVALID_ARGUMENT;
    if (rate->valid) {
        if (rate->dt_us == 0U || rate->dt_us > 20000U)
            return BSP_RTT_INVALID_ARGUMENT;
        for (unsigned i = 0U; i < MIXER_MOTOR_COUNT; ++i) {
            if (rate->mixer.motor[i] < 0 || rate->mixer.motor[i] > NORM_Q15_ONE)
                return BSP_RTT_INVALID_ARGUMENT;
        }
    }

    const bsp_rtt_result_t ready =
        bsp_rtt_prepare_write(BSP_RTT_DATA, RATE_FRAME_BYTES);
    if (ready != BSP_RTT_OK)
        return ready;
    uint8_t frame[RATE_FRAME_BYTES] = {'D', 'R', 'T', '!', 1U, 11U, 54U, 0U};
    put_u32(frame + 8U, sequence);
    put_u32(frame + 12U, rate->previous_us);
    if (rate->valid) {
        put_u16(frame + 16U, (uint16_t)rate->dt_us);
        for (unsigned i = 0U; i < RATE_AXIS_COUNT; ++i) {
            put_float(frame + 18U + 4U * i, rate->setpoint_radps[i]);
            put_float(frame + 30U + 4U * i, rate->gyro_frd_radps[i]);
            put_float(frame + 42U + 4U * i, rate->output[i]);
        }
        for (unsigned i = 0U; i < MIXER_MOTOR_COUNT; ++i)
            put_u16(frame + 54U + 2U * i, (uint16_t)rate->mixer.motor[i]);
    }
    const size_t crc_offset = sizeof(frame) - 4U;
    put_u32(frame + crc_offset, app_crc32(frame, crc_offset));
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
    const uint32_t age_us = sample->t_control_us - sample->t_estimate_us;
    const bool compact = age_us <= ATTITUDE_CONTROL_AGE_MASK;
    const size_t frame_bytes = compact ? ATTITUDE_CONTROL_COMPACT_FRAME_BYTES
                                       : ATTITUDE_CONTROL_FRAME_BYTES;
    const bsp_rtt_result_t ready =
        bsp_rtt_prepare_write(BSP_RTT_DATA, frame_bytes);
    if (ready != BSP_RTT_OK)
        return ready;
    uint8_t frame[ATTITUDE_CONTROL_FRAME_BYTES] = {'D', 'R', 'T', '!', 1U};
    frame[5] = compact ? 12U : 7U;
    put_u16(frame + 6U, (uint16_t)(frame_bytes - 12U));
    put_u32(frame + 8U, sequence);
    put_u32(frame + 12U, sample->t_control_us);
    uint8_t *fields;
    if (compact) {
        const uint16_t age_valid =
            (uint16_t)age_us |
            (sample->valid ? ATTITUDE_CONTROL_VALID_BIT : 0U);
        put_u16(frame + 16U, age_valid);
        fields = frame + 18U;
    } else {
        put_u32(frame + 16U, sample->t_estimate_us);
        put_u32(frame + 20U, sample->valid ? 1U : 0U);
        fields = frame + 24U;
    }
    for (unsigned i = 0; i < 4U; ++i)
        put_float(fields + 4U * i, sample->body_to_ned[i]);
    for (unsigned i = 0; i < 2U; ++i) {
        put_float(fields + 16U + 4U * i, sample->target_rad[i]);
        put_float(fields + 24U + 4U * i,
                  sample->valid ? sample->tilt_error[i] : 0.0f);
        put_float(fields + 32U + 4U * i,
                  sample->valid ? sample->rate_radps[i] : 0.0f);
    }
    const size_t crc_offset = frame_bytes - 4U;
    put_u32(frame + crc_offset, app_crc32(frame, crc_offset));
    return bsp_rtt_write(BSP_RTT_DATA, frame, frame_bytes);
}
