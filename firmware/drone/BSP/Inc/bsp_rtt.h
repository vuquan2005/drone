#ifndef BSP_RTT_H
#define BSP_RTT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef DRONE_RTT
#define DRONE_RTT 0
#endif

typedef enum {
    BSP_RTT_LOG = 0, /**< Human-readable event/error text. */
    BSP_RTT_DATA = 1 /**< Structured records for host tools. */
} bsp_rtt_channel_t;

typedef enum {
    BSP_RTT_OK = 0, /**< Operation succeeded; prepare does not enqueue bytes. */
    BSP_RTT_DISABLED,         /**< RTT transport was compiled out. */
    BSP_RTT_NOT_INITIALIZED,  /**< Call bsp_rtt_init before writing. */
    BSP_RTT_INVALID_ARGUMENT, /**< Invalid channel or NULL nonempty payload. */
    BSP_RTT_TOO_LARGE, /**< Record cannot fit even in an empty channel. */
    BSP_RTT_FULL /**< Valid record skipped because free space was insufficient.
                  */
} bsp_rtt_result_t;

#if DRONE_RTT
/** @brief Initialize both RTT up-channels and reset their dropped-full
 * counters. Discards all queued output.
 * @note Single foreground caller only, after board initialization and before
 * writes. Never run concurrently with a writer or from an ISR. No host is
 * required. Disabled builds make this call a no-op.
 */
void bsp_rtt_init(void);

/** @brief Preflight one intended write before encoding a record.
 * @param channel BSP_RTT_LOG or BSP_RTT_DATA.
 * @param size Encoded size, strictly less than the channel capacity; 0 is
 * valid.
 * @return OK if space is currently sufficient; FULL increments the channel's
 * dropped-full counter exactly once. Other failures follow bsp_rtt_write's
 * channel/init/size rules and never count as a full drop.
 * @note Caller must validate payload before this call. Call once per intended
 * record, not as a free-space polling query. If non-OK, abandon this attempt
 * without calling write. On OK, encode then call bsp_rtt_write, which still
 * checks space. No reservation, locks, waiting or queued bytes are added.
 * Single foreground writer only; do not interleave other writes/init between
 * prepare and write. Host reads may free space; payload remains caller-owned.
 */
bsp_rtt_result_t bsp_rtt_prepare_write(bsp_rtt_channel_t channel, size_t size);

/** @brief Copy one complete record to a channel without waiting for the host.
 * @param channel BSP_RTT_LOG or BSP_RTT_DATA; buffers are independent.
 * @param data Bytes to copy; need not end in NUL. May be NULL only if size is
 * 0.
 * @param size Number of bytes, strictly less than the channel buffer capacity.
 * @return BSP_RTT_OK if queued; otherwise the reason for rejecting the record.
 * A failed write never queues a partial record. Only BSP_RTT_FULL increments
 * the channel's dropped-full counter. Zero-length writes do not consume space.
 * @note Single foreground writer only, never ISR. Call after bsp_rtt_init.
 * Payload lifetime ends when this call returns. No formatting, timestamping,
 * framing, retry or background queue is added. Full channels skip records.
 */
bsp_rtt_result_t bsp_rtt_write(bsp_rtt_channel_t channel, const void *data,
                               size_t size);

/** @brief Read how many valid records were skipped for lack of buffer space.
 * @param channel BSP_RTT_LOG or BSP_RTT_DATA.
 * @return Channel counter since init, wrapping modulo 2^32; 0 before init,
 * for an invalid channel or when RTT is disabled. Counts records, not bytes.
 * @note Foreground-only, serialized with init/write. Success, invalid input,
 * oversized records and disabled/uninitialized transport do not increment it.
 * Does not measure missed sensor samples or downstream host/TCP loss.
 */
uint32_t bsp_rtt_get_dropped_full(bsp_rtt_channel_t channel);
#else
static inline void bsp_rtt_init(void) {}
static inline bsp_rtt_result_t bsp_rtt_prepare_write(bsp_rtt_channel_t channel,
                                                     size_t size) {
    (void)channel;
    (void)size;
    return BSP_RTT_DISABLED;
}
static inline bsp_rtt_result_t bsp_rtt_write(bsp_rtt_channel_t channel,
                                             const void *data, size_t size) {
    (void)channel;
    (void)data;
    (void)size;
    return BSP_RTT_DISABLED;
}
static inline uint32_t bsp_rtt_get_dropped_full(bsp_rtt_channel_t channel) {
    (void)channel;
    return 0U;
}
#endif

#endif
