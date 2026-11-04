#include "crsf.h"

#include <string.h>

#define CRSF_ADDRESS_FLIGHT_CONTROLLER 0xC8U
#define CRSF_TYPE_RC_CHANNELS_PACKED 0x16U
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
