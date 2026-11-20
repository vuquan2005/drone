#ifndef APP_FLASH_RECORDER_H
#define APP_FLASH_RECORDER_H

#include "app_config.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef DRONE_FLASH_RECORD
#define DRONE_FLASH_RECORD 0
#endif

/** Start the recorder and journal in FLIGHT. Time is MCU uptime in ms. */
void app_flash_recorder_init(uint32_t now_ms);

/** Latch a valid RC recording request. Invalid RC retains the prior request.
 * Call before producing telemetry. now_ms timestamps newly queued frames. */
void app_flash_recorder_update(bool valid, bool requested, uint32_t now_ms);

/** Service one journal step and at most one queued frame. Foreground only.
 * @param now_ms MCU uptime in ms.
 * @param get_config Non-NULL provider called once when a session starts. */
void app_flash_recorder_process(uint32_t now_ms,
                                app_config_t (*get_config)(void));

/** Return whether new samples belong to the current flash session. */
bool app_flash_recorder_accepting(void);

/** Check one frame before encoding. A full queue counts one dropped frame.
 * Foreground only. On true, immediately enqueue the complete frame. */
bool app_flash_recorder_prepare(size_t size);

/** Copy a frame after a successful prepare with the same size. No retry. */
void app_flash_recorder_enqueue(const uint8_t *frame, size_t size);

#endif
