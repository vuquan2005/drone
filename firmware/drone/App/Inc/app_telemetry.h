/** @file app_telemetry.h
 * @brief Versioned binary telemetry on RTT channel 1; foreground writer.
 */
#ifndef APP_TELEMETRY_H
#define APP_TELEMETRY_H

#include "app_config.h"
#include "app_imu.h"
#include "app_rate.h"
#include "app_receiver.h"
#include "bsp_rtt.h"

#ifndef DRONE_TELEMETRY
#define DRONE_TELEMETRY 0
#endif

#define APP_TELEMETRY_ENABLED (DRONE_RTT && DRONE_TELEMETRY)

#if APP_TELEMETRY_ENABLED
/** @brief Reset telemetry counters and discard the pending IMU batch.
 * Foreground only, after bsp_rtt_init. Leaves the RTT buffers alone.
 */
void app_telemetry_init(void);

/** @brief Copy a raw IMU sample into the type 9 RTT batch.
 * @param sample Non-NULL successful BSP read, valid for this call. Only raw
 * axes and timestamps are used. A NULL or out-of-range sensor clock is counted
 * as a rejection.
 * @param sequence Wrapping count of successful IMU reads, including samples
 * rejected or dropped by telemetry.
 * @note Nonblocking, serialized foreground only after app_telemetry_init().
 * Sends a batch at eight samples or closes it at a gap, an unrepresentable
 * delta, or an age of 10 ms. A failed write discards the complete batch.
 * Call app_telemetry_process_imu() periodically to send expired partial
 * batches.
 */
void app_telemetry_queue_imu(const app_imu_sample_t *sample, uint32_t sequence);

/** Check for a pending IMU batch at least 10 ms old and try to send it.
 * Call every foreground runtime iteration, including without a fresh sample.
 * Nonblocking and serialized with other telemetry calls.
 */
void app_telemetry_process_imu(void);

/** Poll down-channel 0 for a complete GET_CONFIG request, including partial
 * requests from earlier calls. On success writes the request ID to id.
 * Call in foreground after init, with a non-NULL writable id. */
bool app_telemetry_poll_config_request(uint32_t *id);

/** Try one type 10 response. Config and referenced state must be valid during
 * this foreground call. A full RTT buffer discards the response. */
bsp_rtt_result_t app_telemetry_write_config(uint32_t id,
                                            const app_config_t *config);
/** @brief Enqueue one cumulative RTT/IMU statistics snapshot (type 2).
 * @param uptime_ms MCU time captured by caller, wrapping uint32 milliseconds.
 * @param imu_samples Successful IMU read count since App init.
 * @param imu_read_errors IMU driver-read error count since App init.
 * @return BSP result. One attempt, no retry; failure counts appear in a later
 * snapshot. DATA dropped_full includes both IMU and statistics frames.
 * Preflight rejects insufficient space before building fields or CRC.
 * @note Foreground-only, call about once a second independently of IMU success.
 * Snapshot precedes its own write. Sequence increments for every stats attempt.
 */
bsp_rtt_result_t app_telemetry_write_stats(uint32_t uptime_ms,
                                           uint32_t imu_samples,
                                           uint32_t imu_read_errors);
/** @brief Send a type 8 RC snapshot from serialized foreground context.
 * @param sample Non-NULL snapshot; CH1–CH4 must fit unsigned 11-bit CRSF.
 * CH5–CH8 must also fit when connected and are encoded as switch requests.
 * @param uptime_ms HAL tick captured after obtaining sample, modulo 2^32 ms.
 * @return BSP result; invalid input returns INVALID_ARGUMENT before preflight.
 * Sequence counts attempts since telemetry init, including failures. One write,
 * no retry; disconnected snapshots retain CH1–CH4 and clear the flags byte.
 */
bsp_rtt_result_t
app_telemetry_write_receiver(const app_receiver_sample_t *sample,
                             uint32_t uptime_ms);
/** Enqueue a type 5 computational preview snapshot (92 bytes), at caller's
 * decimated cadence. State must be initialized and serialized with the rate
 * pipeline. NULL returns INVALID_ARGUMENT. Includes configured/valid flags;
 * invalid dynamics are zero after reset. No motor I/O, nonblocking foreground,
 * one preflight/write attempt without retry; generic DATA drops count failures.
 * Sequence counts attempts, timestamp is the last consumed IMU read time (us).
 * Layout and units: protocol/telemetry.md. */
bsp_rtt_result_t app_telemetry_write_rate(const app_rate_t *rate);
/** @brief Send one RTT DATA snapshot of a fresh FRD-to-NED quaternion.
 * @param q Readable unit quaternion in w,x,y,z order.
 * @param t_sample_us MCU timestamp of the estimate, modulo 2^32.
 * @return RTT write result. Foreground only, nonblocking.
 */
bsp_rtt_result_t app_telemetry_write_attitude(const float q[4],
                                              uint32_t t_sample_us);

/** One attitude-controller evaluation and the estimate it used. */
typedef struct {
    uint32_t t_control_us; /**< MCU time at evaluation start, modulo 2^32. */
    uint32_t
        t_estimate_us; /**< MCU IMU read time of the quaternion, modulo 2^32. */
    float body_to_ned[4]; /**< Unit quaternion in w,x,y,z order. */
    float target_rad[2];  /**< Desired Euler roll and pitch in radians. */
    float
        tilt_error[2]; /**< Twice the body X/Y tilt-error quaternion vector. */
    float rate_radps[2]; /**< Requested body X/Y rates before rate limits. */
    bool valid;          /**< Error and rate fields are valid. */
} app_attitude_control_sample_t;

/** @brief Send one attitude-controller sample over RTT DATA.
 * @param sample Readable sample for the duration of the call. NULL is invalid.
 * @return RTT result. Invalid evaluations encode zero error and rate fields.
 * @note Nonblocking foreground only. Sequence counts attempted writes.
 */
bsp_rtt_result_t app_telemetry_write_attitude_control(
    const app_attitude_control_sample_t *sample);

#else
static inline void app_telemetry_init(void) {}
static inline void app_telemetry_queue_imu(const app_imu_sample_t *sample,
                                           uint32_t sequence) {
    (void)sample;
    (void)sequence;
}
static inline void app_telemetry_process_imu(void) {}
#endif

#endif
