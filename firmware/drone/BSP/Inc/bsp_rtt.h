#ifndef BSP_RTT_H
#define BSP_RTT_H

#include <stdbool.h>
#include <stddef.h>

#ifndef DRONE_RTT
#define DRONE_RTT 0
#endif

typedef enum {
    BSP_RTT_LOG = 0, /**< Human-readable event/error text. */
    BSP_RTT_DATA = 1 /**< Structured records for host tools. */
} bsp_rtt_channel_t;

#if DRONE_RTT
/** @brief Initialize both RTT up-channels, discarding queued output.
 * @note Single foreground caller only, after board initialization and before
 * writes. Never run concurrently with a writer or from an ISR. No host is
 * required. Disabled builds make this call a no-op.
 */
void bsp_rtt_init(void);

/** @brief Copy one complete record to a channel without waiting for the host.
 * @param channel BSP_RTT_LOG or BSP_RTT_DATA; buffers are independent.
 * @param data Bytes to copy; need not end in NUL. May be NULL only if size is
 * 0.
 * @param size Number of bytes, strictly less than the channel buffer capacity.
 * @return true if the whole record was queued (including an empty record).
 * False before init, for invalid arguments, insufficient space or disabled RTT.
 * A failed write never queues a partial record; the caller may count drops.
 * @note Single foreground writer only, never ISR. Call after bsp_rtt_init.
 * Payload lifetime ends when this call returns. No formatting, timestamping,
 * framing, retry or background queue is added. Full channels skip records.
 */
bool bsp_rtt_write(bsp_rtt_channel_t channel, const void *data, size_t size);
#else
static inline void bsp_rtt_init(void) {}
static inline bool bsp_rtt_write(bsp_rtt_channel_t channel, const void *data,
                                 size_t size) {
    (void)channel;
    (void)data;
    (void)size;
    return false;
}
#endif

#endif
