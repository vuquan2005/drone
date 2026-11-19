#ifndef CRSF_H
#define CRSF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CRSF_CHANNEL_COUNT 16U
#define CRSF_MAX_FRAME_SIZE 64U
#define CRSF_FRAME_TIMEOUT_MS 3U
#define CRSF_FLIGHT_MODE_TEXT_MAX 13U
#define CRSF_FLIGHT_MODE_FRAME_MAX (CRSF_FLIGHT_MODE_TEXT_MAX + 5U)
#define CRSF_ATTITUDE_FRAME_SIZE 10U

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

/** Encode a CRSF Flight Mode (0x21) broadcast frame.
 * @param label NUL-terminated printable ASCII, 1 to 13 bytes. The text limit
 * keeps the full label visible on EdgeTX 2.11.
 * @param frame Destination buffer, which must not overlap label.
 * @param capacity Writable bytes in frame.
 * @return Frame size in bytes, or zero for invalid input or insufficient
 * capacity. Leaves the destination unchanged on failure. No I/O. */
uint8_t crsf_encode_flight_mode(const char *label, uint8_t *frame,
                                size_t capacity);

/** Encode a CRSF Attitude (0x1E) broadcast frame with pitch, roll and yaw in
 * signed, big-endian units of 100 microradians.
 * @param body_to_ned Readable unit quaternion (w,x,y,z) rotating body FRD to
 * NED. Angles use ZYX Euler rotation and inherit the quaternion's heading.
 * @param frame Destination buffer, which must not overlap body_to_ned.
 * @param capacity Writable bytes in frame.
 * @return Frame size, or zero for a NULL pointer, insufficient capacity or a
 * zero/non-finite quaternion norm. Leaves the destination unchanged on
 * failure. No I/O. */
uint8_t crsf_encode_attitude(const float body_to_ned[4], uint8_t *frame,
                             size_t capacity);

#endif
