#ifndef FLASH_ERASE_H
#define FLASH_ERASE_H

#include "app_command.h"

typedef enum {
    FLASH_ERASE_WAIT,
    FLASH_ERASE_BUSY,
    FLASH_ERASE_DONE,
    FLASH_ERASE_ERROR,
} flash_erase_state_t;

/** Enter the maintenance mode with PWM already disabled. Foreground only. */
void flash_erase_rc_init(void);

/** Start erase and verification after the caller authorizes it.
 * @param now_ms MCU uptime in ms modulo 2^32.
 * @pre PWM is disabled, the caller owns the flash and no SPI transfer is
 * active.
 * @note Foreground only. Continue polling with flash_erase_process().
 */
void flash_erase_start(uint32_t now_ms);

/** Run one bounded erase or verification step after flash_erase_start().
 * @param now_ms MCU uptime in ms modulo 2^32. Foreground only.
 */
void flash_erase_process(uint32_t now_ms);

/** Qualify a fresh LOW to HIGH SA confirmation, then erase and verify.
 * @param command Current decoded RC command, or NULL for invalid RC.
 * @param now_ms MCU uptime in ms modulo 2^32.
 * @note The mode owns the external flash until reset. At most one bounded
 * step per call. Once confirmed, the operation continues through RC loss.
 */
void flash_erase_rc_process(const app_command_t *command, uint32_t now_ms);

/** Return the latched maintenance state for CRSF status. Foreground only. */
flash_erase_state_t flash_erase_get_state(void);

#endif
