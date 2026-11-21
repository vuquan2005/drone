#ifndef FLASH_EXPORT_H
#define FLASH_EXPORT_H

#include "bsp_rtt.h"

#if DRONE_RTT
typedef enum {
    FLASH_EXPORT_WAIT,
    FLASH_EXPORT_BUSY,
    FLASH_EXPORT_DONE,
    FLASH_EXPORT_ERROR,
    FLASH_EXPORT_ERASE_BUSY,
    FLASH_EXPORT_ERASE_DONE,
    FLASH_EXPORT_ERASE_ERROR,
} flash_export_state_t;

/** Initialize the export mode with PWM already disabled. */
void flash_export_init(void);

/** Handle control bytes and one bounded flash or transmit step.
 * @param now_ms MCU uptime in ms modulo 2^32. Foreground only.
 */
void flash_export_process(uint32_t now_ms);

/** Return the mode status for CRSF. DONE means END was queued to RTT. */
flash_export_state_t flash_export_get_state(void);
#endif

#endif
