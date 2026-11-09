#ifndef BSP_RECEIVER_H
#define BSP_RECEIVER_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BSP_RECEIVER_EMPTY,
    BSP_RECEIVER_BYTE,
    BSP_RECEIVER_DISCONTINUITY
} bsp_receiver_result_t;

/** @brief Start/restart board USART1 circular DMA/IDLE reception after HAL UART
 * init.
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

#endif
