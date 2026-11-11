/** @file app_imu_trace.h
 * @brief Versioned, newline-framed IMU records on RTT channel 1.
 */
#ifndef APP_IMU_TRACE_H
#define APP_IMU_TRACE_H

#include "app_imu.h"
#include "bsp_rtt.h"

#ifndef DRONE_IMU_TRACE
#define DRONE_IMU_TRACE 0
#endif

#if DRONE_RTT && DRONE_IMU_TRACE
/** @brief Enqueue one CSV IMU1 record using timestamps already stored in
 * sample.
 * @param sample Non-NULL bounded SI sample produced by app_imu_poll.
 * @param sequence Successful-read counter, including records RTT rejects.
 * @return RTT result; NULL returns INVALID_ARGUMENT, format failure/overflow
 * returns TOO_LARGE. Preserves sample, sequence and timestamps on failure.
 * @note Foreground-only. Fields: IMU1,sequence,t_read_us,sensor_time_ticks,
 * ax_mmps2,ay_mmps2,az_mmps2,gx_mradps,gy_mradps,gz_mradps and newline.
 * Milli-unit conversion truncates toward zero; axes are sensor-package XYZ.
 * Caller sends once per successful sample; no decimation, retry or timestamp
 * generation here. All counters/times retain their original wrapping widths.
 */
bsp_rtt_result_t app_imu_trace_write(const app_imu_sample_t *sample,
                                     uint32_t sequence);
#define APP_IMU_TRACE(sample, sequence)                                        \
    ((void)app_imu_trace_write(sample, sequence))
#else
#define APP_IMU_TRACE(...) ((void)0)
#endif

#endif
