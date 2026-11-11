#include "app_imu_trace.h"
#include <stdio.h>

bsp_rtt_result_t app_imu_trace_write(const app_imu_sample_t *sample,
                                     uint32_t sequence) {
    if (sample == NULL) {
        return BSP_RTT_INVALID_ARGUMENT;
    }
    char line[192];
    int length = snprintf(
        line, sizeof(line), "IMU1,%lu,%lu,%lu,%ld,%ld,%ld,%ld,%ld,%ld\n",
        (unsigned long)sequence, (unsigned long)sample->t_read_us,
        (unsigned long)sample->sensor_time_ticks,
        (long)(sample->accel_mps2[0] * 1000.0f),
        (long)(sample->accel_mps2[1] * 1000.0f),
        (long)(sample->accel_mps2[2] * 1000.0f),
        (long)(sample->gyro_radps[0] * 1000.0f),
        (long)(sample->gyro_radps[1] * 1000.0f),
        (long)(sample->gyro_radps[2] * 1000.0f));
    if (length < 0 || (size_t)length >= sizeof(line)) {
        return BSP_RTT_TOO_LARGE;
    }
    return bsp_rtt_write(BSP_RTT_DATA, line, (size_t)length);
}
