/** @file app_telemetry.h
 * @brief Versioned binary telemetry on RTT channel 1; foreground writer.
 */
#ifndef APP_TELEMETRY_H
#define APP_TELEMETRY_H

#include "app_imu.h"
#include "app_rate.h"
#include "app_receiver.h"
#include "bsp_rtt.h"

#ifndef DRONE_TELEMETRY
#define DRONE_TELEMETRY 0
#endif

#if DRONE_RTT && DRONE_TELEMETRY
/** @brief Reset telemetry attempt sequence and result counters; foreground
 * only. Call after bsp_rtt_init. Does not initialize or clear the RTT
 * transport.
 */
void app_telemetry_init(void);

/** @brief Enqueue one 44-byte binary raw IMU frame v1 type 4 using timestamps
 * already stored in sample.
 * @param sample Non-NULL sample produced by app_imu_poll; reads raw fields
 * only.
 * @param sequence Successful-read counter, including records RTT rejects.
 * @return RTT result; NULL or invalid sensor time returns INVALID_ARGUMENT.
 * Preserves sample, sequence and
 * timestamps on failure.
 * @note Foreground-only. Schema: protocol/telemetry.md (little-endian, CRC32).
 * Fields: sequence, t_read_us, sensor_time_ticks, original int16 sensor XYZ
 * counts, ranges, ODRs and body mapping. A BSP preflight checks
 * space before encoding/CRC; FULL skips the frame and counts one drop. The
 * final write still checks capacity. Entire frame is queued in one BSP call.
 * No SI conversion, rounding or body mapping is applied to counts on the wire.
 * Metadata describes the fixed BSP configuration and FRD mapping [-X, +Y, -Z]
 * (axis_map=2). Caller sends once per successful sample; no decimation, retry
 * or timestamp generation here. All counters/times retain their original
 * wrapping widths.
 */
bsp_rtt_result_t app_telemetry_write_imu(const app_imu_sample_t *sample,
                                         uint32_t sequence);
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
/** @brief Send a type 3 RC snapshot from serialized foreground context.
 * @param sample Non-NULL snapshot; channels must fit unsigned 11-bit CRSF.
 * @param uptime_ms HAL tick captured after obtaining sample, modulo 2^32 ms.
 * @return BSP result; invalid input returns INVALID_ARGUMENT before preflight.
 * Sequence counts attempts since telemetry init, including failures. One write,
 * no retry; disconnected snapshots retain old channels. Not motor failsafe.
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

#define APP_TELEMETRY_RATE(rate) ((void)app_telemetry_write_rate(rate))
#define APP_TELEMETRY_RECEIVER(sample, ms)                                     \
    ((void)app_telemetry_write_receiver(sample, ms))
#define APP_TELEMETRY_STATS(ms, samples, errors)                               \
    ((void)app_telemetry_write_stats(ms, samples, errors))
#define APP_TELEMETRY_IMU(sample, sequence)                                    \
    ((void)app_telemetry_write_imu(sample, sequence))
#else
static inline void app_telemetry_init(void) {}
#define APP_TELEMETRY_RATE(...) ((void)0)
#define APP_TELEMETRY_RECEIVER(...) ((void)0)
#define APP_TELEMETRY_STATS(...) ((void)0)
#define APP_TELEMETRY_IMU(...) ((void)0)
#endif

#endif
