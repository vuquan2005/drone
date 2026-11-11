#include "SEGGER_RTT.h"
#include "bsp_rtt.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned drain(unsigned channel, uint8_t *output, unsigned capacity) {
    SEGGER_RTT_BUFFER_UP *ring = &_SEGGER_RTT.aUp[channel];
    unsigned count = 0;
    while (ring->RdOff != ring->WrOff && count < capacity) {
        output[count++] = (uint8_t)ring->pBuffer[ring->RdOff];
        ring->RdOff = (ring->RdOff + 1U) % ring->SizeOfBuffer;
    }
    return count;
}

int main(void) {
    const uint8_t binary[] = {0x00, 0xFF, 0x0A};
    assert(bsp_rtt_write(BSP_RTT_LOG, binary, sizeof(binary)) ==
           BSP_RTT_NOT_INITIALIZED);
    assert(bsp_rtt_get_dropped_full(BSP_RTT_LOG) == 0U);
    assert(bsp_rtt_get_dropped_full(BSP_RTT_DATA) == 0U);
    bsp_rtt_init();
    assert(strcmp(_SEGGER_RTT.acID, "SEGGER RTT") == 0);
    assert(_SEGGER_RTT.MaxNumUpBuffers == 2);
    assert(bsp_rtt_write((bsp_rtt_channel_t)-1, binary, sizeof(binary)) ==
           BSP_RTT_INVALID_ARGUMENT);
    assert(bsp_rtt_write((bsp_rtt_channel_t)2, binary, sizeof(binary)) ==
           BSP_RTT_INVALID_ARGUMENT);
    assert(bsp_rtt_write(BSP_RTT_LOG, NULL, 1U) == BSP_RTT_INVALID_ARGUMENT);
    assert(bsp_rtt_write(BSP_RTT_LOG, NULL, 0U) == BSP_RTT_OK);
    assert(bsp_rtt_get_dropped_full((bsp_rtt_channel_t)-1) == 0U);
    uint8_t payload[256];
    memset(payload, 0xA5, sizeof(payload));
    uint8_t output[4096];
    for (unsigned channel = 0; channel < 2; ++channel) {
        bsp_rtt_init();
        assert(bsp_rtt_get_dropped_full(BSP_RTT_LOG) == 0U);
        assert(bsp_rtt_get_dropped_full(BSP_RTT_DATA) == 0U);
        assert(_SEGGER_RTT.aUp[channel].Flags == SEGGER_RTT_MODE_NO_BLOCK_SKIP);
        assert(bsp_rtt_write((bsp_rtt_channel_t)channel, payload,
                             _SEGGER_RTT.aUp[channel].SizeOfBuffer) ==
               BSP_RTT_TOO_LARGE);
        assert(bsp_rtt_get_dropped_full((bsp_rtt_channel_t)channel) == 0U);
        unsigned queued = 0;
        bsp_rtt_result_t result;
        while ((result = bsp_rtt_write((bsp_rtt_channel_t)channel, payload,
                                       sizeof(payload))) == BSP_RTT_OK) {
            assert(++queued < 20);
        }
        assert(queued > 0);
        assert(result == BSP_RTT_FULL);
        assert(bsp_rtt_get_dropped_full((bsp_rtt_channel_t)channel) == 1U);
        unsigned before = _SEGGER_RTT.aUp[channel].WrOff;
        assert(bsp_rtt_write((bsp_rtt_channel_t)channel, payload,
                             sizeof(payload)) == BSP_RTT_FULL);
        assert(bsp_rtt_get_dropped_full((bsp_rtt_channel_t)channel) == 2U);
        assert(_SEGGER_RTT.aUp[channel].WrOff == before);
        /* Invalid/oversized/empty records must not look like transport loss,
         * even while the channel lacks space for another full payload. */
        assert(bsp_rtt_write((bsp_rtt_channel_t)channel, NULL, 1U) ==
               BSP_RTT_INVALID_ARGUMENT);
        assert(bsp_rtt_write((bsp_rtt_channel_t)channel, payload,
                             _SEGGER_RTT.aUp[channel].SizeOfBuffer) ==
               BSP_RTT_TOO_LARGE);
        assert(bsp_rtt_write((bsp_rtt_channel_t)channel, NULL, 0U) ==
               BSP_RTT_OK);
        assert(bsp_rtt_get_dropped_full((bsp_rtt_channel_t)channel) == 2U);
        unsigned other = channel ^ 1U;
        assert(bsp_rtt_write((bsp_rtt_channel_t)other, binary,
                             sizeof(binary)) == BSP_RTT_OK);
        assert(bsp_rtt_get_dropped_full((bsp_rtt_channel_t)other) == 0U);
        assert(drain(other, output, sizeof(output)) == sizeof(binary));
        assert(memcmp(output, binary, sizeof(binary)) == 0);
        assert(drain(channel, output, sizeof(output)) ==
               queued * sizeof(payload));
        /* The next record wraps across the ring boundary after the drain. */
        assert(bsp_rtt_write((bsp_rtt_channel_t)channel, payload,
                             sizeof(payload)) == BSP_RTT_OK);
        assert(bsp_rtt_get_dropped_full((bsp_rtt_channel_t)channel) == 2U);
        assert(drain(channel, output, sizeof(output)) == sizeof(payload));
        assert(memcmp(output, payload, sizeof(payload)) == 0);
    }
    bsp_rtt_init();
    assert(bsp_rtt_get_dropped_full(BSP_RTT_LOG) == 0U);
    assert(bsp_rtt_get_dropped_full(BSP_RTT_DATA) == 0U);
    assert(_SEGGER_RTT.aUp[0].RdOff == _SEGGER_RTT.aUp[0].WrOff);
    assert(_SEGGER_RTT.aUp[1].RdOff == _SEGGER_RTT.aUp[1].WrOff);
    puts("RTT transport tests passed");
    return 0;
}
