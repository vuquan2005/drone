/** @file app_telemetry.h
 * @brief Versioned binary telemetry on RTT channel 1; foreground writer.
 */
#ifndef APP_TELEMETRY_H
#define APP_TELEMETRY_H

#include "app_imu.h"
#include "bsp_rtt.h"

#ifndef DRONE_TELEMETRY
#define DRONE_TELEMETRY 0
#endif

#if DRONE_RTT && DRONE_TELEMETRY
/** @brief Enqueue one 48-byte binary IMU frame v1 using timestamps already
 * stored in sample.
 * @param sample Non-NULL bounded SI sample produced by app_imu_poll.
 * @param sequence Successful-read counter, including records RTT rejects.
 * @return RTT result; NULL, invalid sensor time or non-finite/out-of-range
 * milli-units return INVALID_ARGUMENT. Preserves sample, sequence and
 * timestamps on failure.
 * @note Foreground-only. Schema: protocol/telemetry.md (little-endian, CRC32).
 * Fields: sequence, t_read_us, sensor_time_ticks, accel XYZ in mm/s2 and
 * gyro XYZ in mrad/s. Entire frame is queued in one BSP call.
 * Milli-unit conversion truncates toward zero; axes are sensor-package XYZ.
 * Caller sends once per successful sample; no decimation, retry or timestamp
 * generation here. All counters/times retain their original wrapping widths.
 */
bsp_rtt_result_t app_telemetry_write_imu(const app_imu_sample_t *sample,
                                         uint32_t sequence);
#define APP_TELEMETRY_IMU(sample, sequence)                                    \
    ((void)app_telemetry_write_imu(sample, sequence))
#else
#define APP_TELEMETRY_IMU(...) ((void)0)
#endif

#endif
