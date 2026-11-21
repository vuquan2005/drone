#ifndef APP_CRC32_H
#define APP_CRC32_H

#include <stddef.h>
#include <stdint.h>

/** Extend a finalized CRC-32/ISO-HDLC with size readable bytes.
 * Pass zero for the first chunk. */
static inline uint32_t app_crc32_extend(uint32_t previous, const void *data,
                                        size_t size) {
    const uint8_t *bytes = data;
    uint32_t crc = previous ^ UINT32_MAX;
    for (size_t i = 0; i < size; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8U; ++bit) {
            crc = (crc >> 1U) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
        }
    }
    return crc ^ UINT32_MAX;
}

/** Return CRC-32/ISO-HDLC over size readable bytes. */
static inline uint32_t app_crc32(const void *data, size_t size) {
    return app_crc32_extend(0U, data, size);
}

#endif
