#include "imu_calibration_store.h"
#include "accel_calibration.h"
#include "app_crc32.h"
#include "bsp_imu_flash.h"
#include "gyro_bias.h"
#include <math.h>
#include <string.h>

#define IMU_CALIBRATION_VERSION 3U

/* The v3 flash layout is little-endian with binary32 FRD coefficients. */
typedef struct {
    uint32_t sequence;
    uint16_t version;
    uint8_t chip_id;
    uint8_t axis_map;
    uint16_t gyro_range_dps;
    uint16_t accel_range_g;
    float gyro_bias_radps[3];
    float accel_bias_mps2[3];
    float accel_matrix[3][3];
    uint32_t crc32;
} calibration_record_t;

_Static_assert(offsetof(calibration_record_t, gyro_bias_radps) == 12U &&
                   offsetof(calibration_record_t, accel_bias_mps2) == 24U &&
                   offsetof(calibration_record_t, accel_matrix) == 36U &&
                   offsetof(calibration_record_t, crc32) == 72U &&
                   sizeof(calibration_record_t) == 76U,
               "Unexpected IMU calibration flash record layout");

static bool bias_valid(const float bias_radps[3]) {
    for (unsigned axis = 0U; axis < 3U; ++axis) {
        if (!(fabsf(bias_radps[axis]) <= GYRO_BIAS_MAX_RADPS)) {
            return false;
        }
    }
    return true;
}

static bool record_valid(const calibration_record_t *record) {
    return record->version == IMU_CALIBRATION_VERSION &&
           record->chip_id != 0U && record->accel_range_g != 0U &&
           record->crc32 ==
               app_crc32(record, offsetof(calibration_record_t, crc32)) &&
           bias_valid(record->gyro_bias_radps) &&
           accel_calibration_valid(record->accel_bias_mps2,
                                   record->accel_matrix);
}

static void
read_records(calibration_record_t records[BSP_IMU_FLASH_SLOT_COUNT]) {
    for (unsigned slot = 0U; slot < BSP_IMU_FLASH_SLOT_COUNT; ++slot) {
        calibration_record_t *record = &records[slot];
        if (!bsp_imu_flash_read(slot, record, sizeof(*record)) ||
            !record_valid(record)) {
            record->version = 0U;
        }
    }
}

static int
newest_record(const calibration_record_t records[BSP_IMU_FLASH_SLOT_COUNT],
              uint8_t chip_id, bool match_configuration) {
    int newest = -1;
    for (unsigned slot = 0U; slot < BSP_IMU_FLASH_SLOT_COUNT; ++slot) {
        const calibration_record_t *record = &records[slot];
        if (record->version == 0U ||
            (match_configuration &&
             (record->chip_id != chip_id ||
              record->axis_map != APP_IMU_AXIS_MAP_ID ||
              record->gyro_range_dps != (uint16_t)BSP_IMU_GYRO_RANGE_DPS ||
              record->accel_range_g != (uint16_t)BSP_IMU_ACCEL_RANGE_G))) {
            continue;
        }
        if (newest < 0) {
            newest = (int)slot;
        } else {
            const uint32_t delta = record->sequence - records[newest].sequence;
            if (delta != 0U && delta < UINT32_C(0x80000000)) {
                newest = (int)slot;
            }
        }
    }
    return newest;
}

bool imu_calibration_load(uint8_t chip_id, app_imu_calibration_t *calibration) {
    calibration_record_t records[BSP_IMU_FLASH_SLOT_COUNT];
    read_records(records);
    const int newest = newest_record(records, chip_id, true);
    if (newest < 0) {
        return false;
    }
    const calibration_record_t *record = &records[newest];
    memcpy(calibration->gyro_bias_radps, record->gyro_bias_radps,
           sizeof(record->gyro_bias_radps));
    memcpy(calibration->accel_bias_mps2, record->accel_bias_mps2,
           sizeof(record->accel_bias_mps2));
    memcpy(calibration->accel_matrix, record->accel_matrix,
           sizeof(record->accel_matrix));
    calibration->accel_valid = true;
    return true;
}

bool imu_calibration_save(uint8_t chip_id,
                          const app_imu_calibration_t *calibration) {
    if (chip_id == 0U || !bias_valid(calibration->gyro_bias_radps) ||
        !calibration->accel_valid ||
        !accel_calibration_valid(calibration->accel_bias_mps2,
                                 calibration->accel_matrix)) {
        return false;
    }
    calibration_record_t records[BSP_IMU_FLASH_SLOT_COUNT];
    read_records(records);
    const int newest = newest_record(records, chip_id, false);
    const int compatible = newest_record(records, chip_id, true);
    /* Keep the usable copy for this sensor even if the other record is newer.
     */
    const int keep = compatible >= 0 ? compatible : newest;
    const unsigned target = keep == 0 ? 1U : 0U;
    calibration_record_t record = {
        .sequence = newest < 0 ? 1U : records[newest].sequence + 1U,
        .version = IMU_CALIBRATION_VERSION,
        .chip_id = chip_id,
        .axis_map = APP_IMU_AXIS_MAP_ID,
        .gyro_range_dps = (uint16_t)BSP_IMU_GYRO_RANGE_DPS,
        .accel_range_g = (uint16_t)BSP_IMU_ACCEL_RANGE_G,
    };
    memcpy(record.gyro_bias_radps, calibration->gyro_bias_radps,
           sizeof(record.gyro_bias_radps));
    memcpy(record.accel_bias_mps2, calibration->accel_bias_mps2,
           sizeof(record.accel_bias_mps2));
    memcpy(record.accel_matrix, calibration->accel_matrix,
           sizeof(record.accel_matrix));
    record.crc32 = app_crc32(&record, offsetof(calibration_record_t, crc32));
    return bsp_imu_flash_write(target, &record, sizeof(record));
}
