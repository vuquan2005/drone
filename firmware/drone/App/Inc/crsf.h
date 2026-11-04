#ifndef CRSF_H
#define CRSF_H

#include <stdbool.h>
#include <stdint.h>

#define CRSF_CHANNEL_COUNT 16U
#define CRSF_MAX_FRAME_SIZE 64U
#define CRSF_FRAME_TIMEOUT_MS 3U

/** Stream decoder. Own and access from a single execution context. */
typedef struct {
    uint8_t buffer[CRSF_MAX_FRAME_SIZE];
    uint8_t length;
    uint32_t frame_start_ms;
    uint16_t channels[CRSF_CHANNEL_COUNT];
    uint32_t last_frame_ms;
    bool valid;
} crsf_t;

/** Initialize decoder before use. */
void crsf_init(crsf_t *decoder);
/** Feed one byte with a wrapping millisecond timestamp. Returns true for a
 * CRC-valid packed RC frame; channels are raw 11-bit CRSF values. Other frame
 * types are ignored. Total frame assembly time over CRSF_FRAME_TIMEOUT_MS
 * discards a partial frame. All pointers must be non-NULL; feed calls must be
 * serialized. */
bool crsf_feed(crsf_t *decoder, uint8_t byte, uint32_t now_ms);
/** Discard a partial frame after a transport error. */
void crsf_reset_stream(crsf_t *decoder);

#endif
