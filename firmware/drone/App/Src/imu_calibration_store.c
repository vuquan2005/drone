#include "imu_calibration_store.h"
#include "app_crc32.h"
#include "bsp_imu_flash.h"
#include "gyro_bias.h"
#include <math.h>
#include <string.h>

#define IMU_CALIBRATION_VERSION 1U

/* Flash format v1, little-endian with binary32 bias values in body FRD. */
typedef struct {
    uint32_t sequence;
    uint16_t version;
    uint8_t chip_id;
    uint8_t axis_map;
    uint16_t gyro_range_dps;
    uint16_t reserved;
    float gyro_bias_radps[3];
    uint32_t crc32;
} bias_record_t;

_Static_assert(offsetof(bias_record_t, crc32) == 24U &&
                   sizeof(bias_record_t) == 28U,
               "Unexpected gyro bias flash record layout");

static bool bias_valid(const float bias_radps[3]) {
    for (unsigned axis = 0U; axis < 3U; ++axis) {
        if (!(fabsf(bias_radps[axis]) <= GYRO_BIAS_MAX_RADPS)) {
            return false;
        }
    }
    return true;
}

static void read_records(bias_record_t records[BSP_IMU_FLASH_SLOT_COUNT]) {
    for (unsigned slot = 0U; slot < BSP_IMU_FLASH_SLOT_COUNT; ++slot) {
        bias_record_t *record = &records[slot];
        if (!bsp_imu_flash_read(slot, record, sizeof(*record)) ||
            record->version != IMU_CALIBRATION_VERSION ||
            record->chip_id == 0U || record->reserved != 0U ||
            record->crc32 !=
                app_crc32(record, offsetof(bias_record_t, crc32)) ||
            !bias_valid(record->gyro_bias_radps)) {
            record->version = 0U;
        }
    }
}

static int newest_record(const bias_record_t records[BSP_IMU_FLASH_SLOT_COUNT],
                         uint8_t chip_id, bool match_configuration) {
    int newest = -1;
    for (unsigned slot = 0U; slot < BSP_IMU_FLASH_SLOT_COUNT; ++slot) {
        const bias_record_t *record = &records[slot];
        if (record->version == 0U ||
            (match_configuration &&
             (record->chip_id != chip_id ||
              record->axis_map != APP_IMU_AXIS_MAP_ID ||
              record->gyro_range_dps != (uint16_t)BSP_IMU_GYRO_RANGE_DPS))) {
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
    bias_record_t records[BSP_IMU_FLASH_SLOT_COUNT];
    read_records(records);
    const int newest = newest_record(records, chip_id, true);
    if (newest < 0) {
        return false;
    }
    memcpy(calibration->gyro_bias_radps, records[newest].gyro_bias_radps,
           sizeof(records[newest].gyro_bias_radps));
    return true;
}

bool imu_calibration_save(uint8_t chip_id,
                          const app_imu_calibration_t *calibration) {
    if (chip_id == 0U || !bias_valid(calibration->gyro_bias_radps)) {
        return false;
    }
    bias_record_t records[BSP_IMU_FLASH_SLOT_COUNT];
    read_records(records);
    const int newest = newest_record(records, chip_id, false);
    const int compatible = newest_record(records, chip_id, true);
    /* Keep the usable copy for this sensor even if the other record is newer.
     */
    const int keep = compatible >= 0 ? compatible : newest;
    const unsigned target = keep == 0 ? 1U : 0U;
    bias_record_t record = {
        .sequence = newest < 0 ? 1U : records[newest].sequence + 1U,
        .version = IMU_CALIBRATION_VERSION,
        .chip_id = chip_id,
        .axis_map = APP_IMU_AXIS_MAP_ID,
        .gyro_range_dps = (uint16_t)BSP_IMU_GYRO_RANGE_DPS,
    };
    memcpy(record.gyro_bias_radps, calibration->gyro_bias_radps,
           sizeof(record.gyro_bias_radps));
    record.crc32 = app_crc32(&record, offsetof(bias_record_t, crc32));
    return bsp_imu_flash_write(target, &record, sizeof(record));
}
