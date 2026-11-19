#include "imu_calibration_store.h"
#include "accel_calibration.h"
#include "app_crc32.h"
#include "bsp_imu_flash.h"
#include "gyro_bias.h"
#include <math.h>
#include <string.h>

#define IMU_CALIBRATION_V1 1U
#define IMU_CALIBRATION_V2 2U

/* Both flash formats are little-endian and hold binary32 FRD coefficients. */
typedef struct {
    uint32_t sequence;
    uint16_t version;
    uint8_t chip_id;
    uint8_t axis_map;
    uint16_t gyro_range_dps;
    uint16_t accel_range_g; /* Reserved and zero in v1. */
    float gyro_bias_radps[3];
} record_header_t;

typedef struct {
    record_header_t header;
    uint32_t crc32;
} calibration_record_v1_t;

typedef struct {
    record_header_t header;
    float accel_bias_mps2[3];
    float accel_scale[3];
    uint32_t crc32;
} calibration_record_t;

_Static_assert(offsetof(calibration_record_v1_t, crc32) == 24U &&
                   sizeof(calibration_record_v1_t) == 28U &&
                   offsetof(calibration_record_t, crc32) == 48U &&
                   sizeof(calibration_record_t) == 52U,
               "Unexpected IMU calibration flash record layout");

static bool bias_valid(const float bias_radps[3]) {
    for (unsigned axis = 0U; axis < 3U; ++axis) {
        if (!(fabsf(bias_radps[axis]) <= GYRO_BIAS_MAX_RADPS)) {
            return false;
        }
    }
    return true;
}

static bool read_record(unsigned slot, calibration_record_t *record) {
    union {
        calibration_record_v1_t v1;
        calibration_record_t v2;
    } encoded;
    if (!bsp_imu_flash_read(slot, &encoded, sizeof(encoded))) {
        return false;
    }
    const record_header_t *header = &encoded.v2.header;
    if (header->chip_id == 0U || !bias_valid(header->gyro_bias_radps)) {
        return false;
    }
    if (header->version == IMU_CALIBRATION_V1) {
        if (header->accel_range_g != 0U ||
            encoded.v1.crc32 !=
                app_crc32(&encoded.v1,
                          offsetof(calibration_record_v1_t, crc32))) {
            return false;
        }
        *record = (calibration_record_t){.header = *header};
        return true;
    }
    if (header->version != IMU_CALIBRATION_V2 || header->accel_range_g == 0U ||
        encoded.v2.crc32 !=
            app_crc32(&encoded.v2, offsetof(calibration_record_t, crc32)) ||
        !accel_calibration_valid(encoded.v2.accel_bias_mps2,
                                 encoded.v2.accel_scale)) {
        return false;
    }
    *record = encoded.v2;
    return true;
}

static void
read_records(calibration_record_t records[BSP_IMU_FLASH_SLOT_COUNT]) {
    for (unsigned slot = 0U; slot < BSP_IMU_FLASH_SLOT_COUNT; ++slot) {
        if (!read_record(slot, &records[slot])) {
            records[slot].header.version = 0U;
        }
    }
}

static int
newest_record(const calibration_record_t records[BSP_IMU_FLASH_SLOT_COUNT],
              uint8_t chip_id, bool match_configuration) {
    int newest = -1;
    for (unsigned slot = 0U; slot < BSP_IMU_FLASH_SLOT_COUNT; ++slot) {
        const record_header_t *record = &records[slot].header;
        if (record->version == 0U ||
            (match_configuration &&
             (record->chip_id != chip_id ||
              record->axis_map != APP_IMU_AXIS_MAP_ID ||
              record->gyro_range_dps != (uint16_t)BSP_IMU_GYRO_RANGE_DPS ||
              (record->version == IMU_CALIBRATION_V2 &&
               record->accel_range_g != (uint16_t)BSP_IMU_ACCEL_RANGE_G)))) {
            continue;
        }
        if (newest < 0) {
            newest = (int)slot;
        } else {
            const uint32_t delta =
                record->sequence - records[newest].header.sequence;
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
    memcpy(calibration->gyro_bias_radps, record->header.gyro_bias_radps,
           sizeof(record->header.gyro_bias_radps));
    memcpy(calibration->accel_bias_mps2, record->accel_bias_mps2,
           sizeof(record->accel_bias_mps2));
    memcpy(calibration->accel_scale, record->accel_scale,
           sizeof(record->accel_scale));
    calibration->accel_valid = record->header.version == IMU_CALIBRATION_V2;
    return true;
}

bool imu_calibration_save(uint8_t chip_id,
                          const app_imu_calibration_t *calibration) {
    if (chip_id == 0U || !bias_valid(calibration->gyro_bias_radps) ||
        !calibration->accel_valid ||
        !accel_calibration_valid(calibration->accel_bias_mps2,
                                 calibration->accel_scale)) {
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
        .header =
            {
                .sequence =
                    newest < 0 ? 1U : records[newest].header.sequence + 1U,
                .version = IMU_CALIBRATION_V2,
                .chip_id = chip_id,
                .axis_map = APP_IMU_AXIS_MAP_ID,
                .gyro_range_dps = (uint16_t)BSP_IMU_GYRO_RANGE_DPS,
                .accel_range_g = (uint16_t)BSP_IMU_ACCEL_RANGE_G,
            },
    };
    memcpy(record.header.gyro_bias_radps, calibration->gyro_bias_radps,
           sizeof(record.header.gyro_bias_radps));
    memcpy(record.accel_bias_mps2, calibration->accel_bias_mps2,
           sizeof(record.accel_bias_mps2));
    memcpy(record.accel_scale, calibration->accel_scale,
           sizeof(record.accel_scale));
    record.crc32 = app_crc32(&record, offsetof(calibration_record_t, crc32));
    return bsp_imu_flash_write(target, &record, sizeof(record));
}
