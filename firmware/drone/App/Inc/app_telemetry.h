/** @file app_telemetry.h
 * @brief Versioned binary telemetry on RTT channel 1; foreground writer.
 */
#ifndef APP_TELEMETRY_H
#define APP_TELEMETRY_H

#include "app_imu.h"
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

/** @brief Enqueue one 48-byte binary IMU frame v1 using timestamps already
 * stored in sample.
 * @param sample Non-NULL bounded SI sample produced by app_imu_poll.
 * @param sequence Successful-read counter, including records RTT rejects.
 * @return RTT result; NULL, invalid sensor time or non-finite/out-of-range
 * milli-units return INVALID_ARGUMENT. Preserves sample, sequence and
 * timestamps on failure.
 * @note Foreground-only. Schema: protocol/telemetry.md (little-endian, CRC32).
 * Fields: sequence, t_read_us, sensor_time_ticks, accel XYZ in mm/s2 and
 * gyro XYZ in mrad/s. After validating/scaling input, a BSP preflight checks
 * space before encoding/CRC; FULL skips the frame and counts one drop. The
 * final write still checks capacity. Entire frame is queued in one BSP call.
 * Milli-unit conversion truncates toward zero; axes are sensor-package XYZ.
 * Caller sends once per successful sample; no decimation, retry or timestamp
 * generation here. All counters/times retain their original wrapping widths.
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
#define APP_TELEMETRY_RECEIVER(sample, ms)                                     \
    ((void)app_telemetry_write_receiver(sample, ms))
#define APP_TELEMETRY_STATS(ms, samples, errors)                               \
    ((void)app_telemetry_write_stats(ms, samples, errors))
#define APP_TELEMETRY_IMU(sample, sequence)                                    \
    ((void)app_telemetry_write_imu(sample, sequence))
#else
static inline void app_telemetry_init(void) {}
#define APP_TELEMETRY_RECEIVER(...) ((void)0)
#define APP_TELEMETRY_STATS(...) ((void)0)
#define APP_TELEMETRY_IMU(...) ((void)0)
#endif

#endif
