#include "crsf.h"
#include <assert.h>
#include <stdio.h>

/* Known CRSF frame: 16 packed channels, each at raw center 992. */
static const uint8_t frame[] = {0xc8, 0x18, 0x16, 0xe0, 0x03, 0x1f, 0xf8,
                                0xc0, 0x07, 0x3e, 0xf0, 0x81, 0x0f, 0x7c,
                                0xe0, 0x03, 0x1f, 0xf8, 0xc0, 0x07, 0x3e,
                                0xf0, 0x81, 0x0f, 0x7c, 0xad};

int main(void) {
    crsf_t decoder;
    crsf_init(&decoder);
    for (unsigned i = 0; i < sizeof(frame); i++) {
        bool received = crsf_feed(&decoder, frame[i], 10);
        assert(received == (i == sizeof(frame) - 1));
    }
    assert(decoder.valid && decoder.last_frame_ms == 10);
    for (unsigned i = 0; i < CRSF_CHANNEL_COUNT; i++)
        assert(decoder.channels[i] == 992);
    // TODO: Snapshot and assert channels/valid as well as freshness after
    // corrupt frames; modifying a stored channel during resynchronization still
    // passes.
    for (unsigned i = 0; i < sizeof(frame); i++) {
        assert(!crsf_feed(&decoder, i == 4 ? frame[i] ^ 1 : frame[i], 11));
    }
    assert(decoder.last_frame_ms == 10);
    crsf_feed(&decoder, 0xc8, 20);
    crsf_feed(&decoder, 0x18, 20);
    for (unsigned i = 0; i < sizeof(frame); i++) {
        bool received = crsf_feed(&decoder, frame[i], 25);
        assert(received == (i == sizeof(frame) - 1));
    }
    assert(decoder.last_frame_ms == 25);
    static const uint8_t varied[] = {0xc8, 0x18, 0x16, 0xdf, 0xf5, 0x6e, 0x77,
                                     0xb9, 0x5b, 0x5e, 0xf2, 0x8e, 0x57, 0xbc,
                                     0xe1, 0x05, 0xef, 0x77, 0xbd, 0xdb, 0x5d,
                                     0xee, 0xa2, 0x94, 0xf2, 0x28};
    static const uint16_t expected[] = {1503, 1502, 1501, 1500, 1509, 1508,
                                        1507, 1506, 1505, 1504, 1503, 1502,
                                        1501, 1500, 1320, 1940};
    for (unsigned i = 0; i < sizeof(varied); i++)
        crsf_feed(&decoder, varied[i], 30);
    assert(decoder.last_frame_ms == 30);
    for (unsigned i = 0; i < CRSF_CHANNEL_COUNT; i++)
        assert(decoder.channels[i] == expected[i]);
    // A corrupt outer frame can contain a complete good frame plus trailing
    // bytes. Recovery must consume only the inner frame, retaining its suffix.
    crsf_init(&decoder);
    crsf_feed(&decoder, 0xc8, 50);
    crsf_feed(&decoder, 30, 50);
    for (unsigned i = 0; i < sizeof(frame); i++)
        crsf_feed(&decoder, frame[i], 50);
    crsf_feed(&decoder, 0xc8, 50);
    crsf_feed(&decoder, 0x18, 50);
    crsf_feed(&decoder, 0x16, 50);
    assert(crsf_feed(&decoder, frame[3], 50));
    assert(decoder.length == 4 && decoder.last_frame_ms == 50);
    for (unsigned i = 4; i < sizeof(frame); i++)
        crsf_feed(&decoder, frame[i], 50);
    assert(decoder.valid && decoder.length == 0);
    // Reject slow trickles even though individual inter-byte gaps are small.
    // TODO: Check complete frames spanning exactly 3 ms (accepted) and 4 ms
    // (rejected); changing the timeout comparison from > to >= still passes.
    crsf_init(&decoder);
    for (unsigned i = 0; i < sizeof(frame); i++)
        assert(!crsf_feed(&decoder, frame[i], i));
    assert(!decoder.valid);
    // Modulo arithmetic at the timestamp wrap.
    crsf_init(&decoder);
    for (unsigned i = 0; i < sizeof(frame); i++)
        crsf_feed(&decoder, frame[i], i < 13 ? UINT32_MAX : 0);
    assert(decoder.valid && decoder.last_frame_ms == 0);
    // Invalid lengths, noise, truncated frames and explicit transport reset.
    crsf_init(&decoder);
    const uint8_t noise[] = {0, 0xff, 0xc8, 0, 0xc8, 1, 0xc8, 63, 0xc8, 255};
    for (unsigned i = 0; i < sizeof(noise); i++)
        assert(!crsf_feed(&decoder, noise[i], 10));
    crsf_reset_stream(&decoder);
    for (unsigned i = 0; i < 5; i++)
        crsf_feed(&decoder, frame[i], 10);
    for (unsigned i = 0; i < sizeof(frame); i++)
        crsf_feed(&decoder, frame[i], 14);
    assert(decoder.valid && decoder.last_frame_ms == 14);
    // Deterministic noise stresses length handling and resynchronization.
    uint32_t random = 1;
    for (unsigned i = 0; i < 100000; i++) {
        random = random * 1664525U + 1013904223U;
        crsf_feed(&decoder, (uint8_t)(random >> 24), i / 32);
        assert(decoder.length < CRSF_MAX_FRAME_SIZE);
    }
    for (unsigned i = 0; i < sizeof(frame); i++)
        crsf_feed(&decoder, frame[i], 4000);
    assert(decoder.last_frame_ms == 4000);
    // CRC-valid non-RC and undersized RC frames never renew RC freshness.
    // TODO: Add a CRC-valid oversized RC frame; accepting lengths >= 24 instead
    // of exactly 24 currently passes. Assert stored channels remain unchanged.
    const uint8_t ignored[] = {0xc8, 2, 0, 0, 0xc8, 3, 0x16, 0, 138};
    uint32_t last_rc = decoder.last_frame_ms;
    for (unsigned i = 0; i < sizeof(ignored); i++)
        assert(!crsf_feed(&decoder, ignored[i], 4001));
    assert(decoder.last_frame_ms == last_rc);
    static const uint8_t zero[] = {
        0xc8, 0x18, 0x16, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
        0x0,  0x0,  0x0,  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xef};
    for (unsigned i = 0; i < sizeof(zero); i++)
        crsf_feed(&decoder, zero[i], 4010);
    for (unsigned i = 0; i < CRSF_CHANNEL_COUNT; i++)
        assert(decoder.channels[i] == 0);
    static const uint8_t full[] = {0xc8, 0x18, 0x16, 0xff, 0xff, 0xff, 0xff,
                                   0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                   0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                   0xff, 0xff, 0xff, 0xff, 0x8f};
    for (unsigned i = 0; i < sizeof(full); i++)
        crsf_feed(&decoder, full[i], 4010);
    for (unsigned i = 0; i < CRSF_CHANNEL_COUNT; i++)
        assert(decoder.channels[i] == 2047);
    puts("CRSF tests passed");
}
