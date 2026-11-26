#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "app_imu.h"
#include "app_rate.h"

enum { APP_CONFIG_PAYLOAD_BYTES = 204U };

typedef enum {
    APP_CONFIG_FLIGHT = 1,
    APP_CONFIG_ESC_CALIB = 2,
    APP_CONFIG_FLASH_EXPORT = 3,
    APP_CONFIG_IMU_CALIB = 4,
} app_config_mode_t;

/** Effective application settings. The IMU codes are driver settings, not a
 * register readback. An inactive group's fields are serialized as zero. */
typedef struct {
    app_config_mode_t mode;
    const app_imu_t *imu;
    bool calibration_from_flash;
    const app_rate_config_t *rate;
    uint32_t imu_timeout_us;
    uint16_t mahony_hz, attitude_hz;
    float mahony_kp_per_s, mahony_ki_per_s2;
    float attitude_kp_per_s, attitude_max_rad;
    float gravity_min_mps2, gravity_max_mps2;
} app_config_t;

/** Encode the version 1 CONFIG payload in little-endian byte order.
 * Both pointers and any referenced state must be readable for the call, with
 * out distinct and writable for APP_CONFIG_PAYLOAD_BYTES bytes. NULL imu or
 * rate fields denote an unavailable group. No I/O or state changes. */
void app_config_encode(uint8_t out[APP_CONFIG_PAYLOAD_BYTES],
                       const app_config_t *config);

#endif
