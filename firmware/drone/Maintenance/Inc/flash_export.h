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

/** Initialize the exporter during boot with PWM already disabled. */
void flash_export_init(void);

/** Read a bounded RTT control chunk during boot selection. Only a complete
 * ELISTS request can select export. On true, enter export mode and pass
 * control to flash_export_process without reinitializing the pending request.
 * Foreground only, after flash_export_init.
 */
bool flash_export_poll_boot_request(void);

/** Handle control bytes and one bounded flash or transmit step.
 * @param now_ms MCU uptime in ms modulo 2^32. Foreground only.
 */
void flash_export_process(uint32_t now_ms);

/** Return the maintenance state. DONE means END was queued to RTT. */
flash_export_state_t flash_export_get_state(void);
#endif

#endif
