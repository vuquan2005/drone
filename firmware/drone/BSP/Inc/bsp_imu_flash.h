#ifndef BSP_IMU_FLASH_H
#define BSP_IMU_FLASH_H

#include <stdbool.h>
#include <stddef.h>

#define BSP_IMU_FLASH_SLOT_COUNT 2U

/** Copy a finalized slot into data, or return false leaving data untouched.
 * Slot is zero-based. Data must be non-NULL and size a positive even byte
 * count that fits in one page with its marker. Serialized foreground only.
 */
bool bsp_imu_flash_read(unsigned slot, void *data, size_t size);

/** Erase the selected slot, write and verify data, then finalize the slot.
 * Uses the same arguments as read. The caller must keep motor outputs off
 * and preserve any previous valid record in the other slot. Blocking,
 * serialized foreground only. Returns true after verifying the marker.
 */
bool bsp_imu_flash_write(unsigned slot, const void *data, size_t size);

#endif
