#include "crsf.h"

#include <math.h>
#include <string.h>

#define CRSF_ADDRESS_FLIGHT_CONTROLLER 0xC8U
#define CRSF_TYPE_RC_CHANNELS_PACKED 0x16U
#define CRSF_TYPE_ATTITUDE 0x1EU
#define CRSF_TYPE_FLIGHT_MODE 0x21U
#define CRSF_RC_FRAME_LENGTH 24U

void crsf_init(crsf_t *decoder) {
    memset(decoder, 0, sizeof(*decoder));
}

void crsf_reset_stream(crsf_t *decoder) {
    decoder->length = 0;
}

static uint8_t crc8(const uint8_t *data, uint8_t length) {
    uint8_t crc = 0;
    for (uint8_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            crc = (uint8_t)((crc & 0x80U) ? (crc << 1) ^ 0xD5U
                                          : (uint32_t)crc << 1);
        }
    }
    return crc;
}

uint8_t crsf_encode_flight_mode(const char *label, uint8_t *frame,
                                size_t capacity) {
    if (label == NULL || frame == NULL)
        return 0;
    uint8_t length = 0;
    while (length < CRSF_FLIGHT_MODE_TEXT_MAX && label[length] != '\0') {
        const uint8_t byte = (uint8_t)label[length];
        if (byte < 0x20U || byte > 0x7eU)
            return 0;
        length++;
    }
    if (length == 0U || label[length] != '\0' || capacity < length + 5U)
        return 0;

    frame[0] = CRSF_ADDRESS_FLIGHT_CONTROLLER;
    frame[1] = (uint8_t)(length + 3U);
    frame[2] = CRSF_TYPE_FLIGHT_MODE;
    memcpy(&frame[3], label, length + 1U);
    frame[length + 4U] = crc8(&frame[2], (uint8_t)(length + 2U));
    return (uint8_t)(length + 5U);
}

static void put_angle_rad(uint8_t *out, float angle_rad) {
    const uint16_t bits = (uint16_t)(int16_t)lroundf(angle_rad * 10000.0f);
    out[0] = (uint8_t)(bits >> 8U);
    out[1] = (uint8_t)bits;
}

uint8_t crsf_encode_attitude(const float body_to_ned[4], uint8_t *frame,
                             size_t capacity) {
    if (body_to_ned == NULL || frame == NULL ||
        capacity < CRSF_ATTITUDE_FRAME_SIZE)
        return 0;

    const float w = body_to_ned[0];
    const float x = body_to_ned[1];
    const float y = body_to_ned[2];
    const float z = body_to_ned[3];
    const float ww = w * w, xx = x * x, yy = y * y, zz = z * z;
    const float norm_sq = ww + xx + yy + zz;
    if (!isfinite(norm_sq) || norm_sq <= 0.0f)
        return 0;

    /* ZYX angles from the body-to-NED rotation matrix. At vertical pitch,
     * roll and yaw share one degree of freedom. Use zero roll. */
    const float r00 = (ww + xx) - (yy + zz);
    const float r10 = 2.0f * (x * y + w * z);
    const float horizontal = hypotf(r00, r10);
    const float pitch_rad = atan2f(2.0f * (w * y - x * z), horizontal);
    float roll_rad = 0.0f;
    float yaw_rad;
    if (horizontal > 0.0f) {
        roll_rad = atan2f(2.0f * (w * x + y * z), (ww + zz) - (xx + yy));
        yaw_rad = atan2f(r10, r00);
    } else {
        yaw_rad = atan2f(2.0f * (w * z - x * y), (ww + yy) - (xx + zz));
    }

    frame[0] = CRSF_ADDRESS_FLIGHT_CONTROLLER;
    frame[1] = CRSF_ATTITUDE_FRAME_SIZE - 2U;
    frame[2] = CRSF_TYPE_ATTITUDE;
    put_angle_rad(&frame[3], pitch_rad);
    put_angle_rad(&frame[5], roll_rad);
    put_angle_rad(&frame[7], yaw_rad);
    frame[9] = crc8(&frame[2], CRSF_ATTITUDE_FRAME_SIZE - 3U);
    return CRSF_ATTITUDE_FRAME_SIZE;
}

bool crsf_feed(crsf_t *decoder, uint8_t byte, uint32_t now_ms) {
    if (decoder->length &&
        (uint32_t)(now_ms - decoder->frame_start_ms) > CRSF_FRAME_TIMEOUT_MS) {
        decoder->length = 0;
    }
    if (!decoder->length && byte != CRSF_ADDRESS_FLIGHT_CONTROLLER) {
        return false;
    }
    if (!decoder->length) {
        decoder->frame_start_ms = now_ms;
    }
    decoder->buffer[decoder->length++] = byte;
    bool received = false;
    while (decoder->length >= 2U) {
        uint8_t frame_length = decoder->buffer[1];
        if (frame_length >= 2U && frame_length <= CRSF_MAX_FRAME_SIZE - 2U) {
            uint8_t total = (uint8_t)(frame_length + 2U);
            if (decoder->length < total) {
                return received;
            }
            if (crc8(&decoder->buffer[2], (uint8_t)(frame_length - 1U)) ==
                decoder->buffer[total - 1U]) {
                bool rc = frame_length == CRSF_RC_FRAME_LENGTH &&
                          decoder->buffer[2] == CRSF_TYPE_RC_CHANNELS_PACKED;
                if (rc) {
                    for (uint8_t i = 0; i < CRSF_CHANNEL_COUNT; i++) {
                        uint16_t offset = (uint16_t)(i * 11U);
                        uint8_t index = (uint8_t)(offset / 8U);
                        uint32_t bits =
                            (uint32_t)decoder->buffer[3U + index] |
                            ((uint32_t)decoder->buffer[4U + index] << 8);
                        if (index + 2U < 22U) {
                            bits |= (uint32_t)decoder->buffer[5U + index] << 16;
                        }
                        decoder->channels[i] =
                            (uint16_t)((bits >> (offset % 8U)) & 0x7ffU);
                    }
                    decoder->last_frame_ms = now_ms;
                    decoder->valid = true;
                }
                received |= rc;
                decoder->length = (uint8_t)(decoder->length - total);
                memmove(decoder->buffer, &decoder->buffer[total],
                        decoder->length);
                continue;
            }
        }
        uint8_t start = 1;
        while (start < decoder->length &&
               decoder->buffer[start] != CRSF_ADDRESS_FLIGHT_CONTROLLER) {
            start++;
        }
        decoder->length = (uint8_t)(decoder->length - start);
        memmove(decoder->buffer, &decoder->buffer[start], decoder->length);
        decoder->frame_start_ms = now_ms;
    }
    return received;
}
