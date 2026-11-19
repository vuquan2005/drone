#ifndef BSP_RECEIVER_H
#define BSP_RECEIVER_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BSP_RECEIVER_EMPTY,
    BSP_RECEIVER_BYTE,
    BSP_RECEIVER_DISCONTINUITY
} bsp_receiver_result_t;

/** @brief Start/restart board receiver circular DMA/IDLE reception after HAL
 * UART init.
 * @return Whether reception started. Call in foreground only. */
bool bsp_receiver_init(void);
/** @brief Pop a byte and its DMA event HAL millisecond tick, or report lost
 * stream data.
 * @param byte Non-NULL output, written only for BSP_RECEIVER_BYTE.
 * @param timestamp_ms Non-NULL output; wraps modulo 2^32. Shared by bytes
 * in one IDLE/half/full batch, not the foreground read time. At 420 kbaud,
 * half-buffer latency is about 0.76 ms plus IRQ latency and tick quantization.
 * Service DMA IRQs within 0.76 ms; a missed DMA lap cannot be recovered.
 * @return A discontinuity flushes queued bytes after overflow/UART error.
 * Nonblocking, foreground only. Also retries failed receive requests. */
bsp_receiver_result_t bsp_receiver_read(uint8_t *byte, uint32_t *timestamp_ms);

/** @brief Copy a frame into the USART1 TX buffer and start interrupt-driven
 * transmission alongside RX DMA.
 * @param frame Readable bytes for this call only. Must be non-NULL.
 * @param length Frame size in bytes, from 1 to 64.
 * @return True if TX started. Invalid input, busy TX or a pending UART error
 * returns false without queuing a frame. Call after init, serialized in
 * foreground only. Does not wait for transmission. */
bool bsp_receiver_try_send(const uint8_t *frame, uint8_t length);

#endif
