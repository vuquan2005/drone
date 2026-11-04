#ifndef BSP_RECEIVER_H
#define BSP_RECEIVER_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BSP_RECEIVER_EMPTY,
    BSP_RECEIVER_BYTE,
    BSP_RECEIVER_DISCONTINUITY
} bsp_receiver_result_t;

/** @brief Start/restart board USART1 byte reception after HAL UART init.
 * @return Whether reception started. Call in foreground only. */
bool bsp_receiver_init(void);
/** @brief Pop a byte and its HAL millisecond tick, or report lost stream data.
 * @param byte Non-NULL output, written only for BSP_RECEIVER_BYTE.
 * @param timestamp_ms Non-NULL output; wraps modulo 2^32.
 * @return A discontinuity flushes queued bytes after overflow/UART error.
 * Nonblocking, foreground only. Also retries failed receive requests. */
bsp_receiver_result_t bsp_receiver_read(uint8_t *byte, uint32_t *timestamp_ms);

#endif
