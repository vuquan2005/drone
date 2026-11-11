#include "bsp_rtt.h"
#include "SEGGER_RTT.h"

static char data_buffer[BSP_RTT_DATA_BUFFER_SIZE];
static bool initialized;
/* Volatile keeps these two diagnostic words visible to live SWD reads.
 * It does not make multiple firmware writers safe. */
static volatile uint32_t dropped_full[2];

void bsp_rtt_init(void) {
    dropped_full[BSP_RTT_LOG] = 0U;
    dropped_full[BSP_RTT_DATA] = 0U;
    SEGGER_RTT_Init();
    initialized = SEGGER_RTT_ConfigUpBuffer(BSP_RTT_DATA, "Data", data_buffer,
                                            sizeof(data_buffer),
                                            SEGGER_RTT_MODE_NO_BLOCK_SKIP) == 0;
}

bsp_rtt_result_t bsp_rtt_write(bsp_rtt_channel_t channel, const void *data,
                               size_t size) {
    if ((channel != BSP_RTT_LOG && channel != BSP_RTT_DATA) ||
        (data == NULL && size != 0U)) {
        return BSP_RTT_INVALID_ARGUMENT;
    }
    if (!initialized) {
        return BSP_RTT_NOT_INITIALIZED;
    }
    size_t capacity =
        channel == BSP_RTT_LOG ? BUFFER_SIZE_UP : sizeof(data_buffer);
    if (size >= capacity) {
        return BSP_RTT_TOO_LARGE;
    }
    if (size == 0U) {
        return BSP_RTT_OK;
    }
    /* One foreground writer: avoid masking RX interrupts during the copy. */
    if (SEGGER_RTT_WriteNoLock((unsigned)channel, data, (unsigned)size) !=
        size) {
        dropped_full[channel]++;
        return BSP_RTT_FULL;
    }
    return BSP_RTT_OK;
}

uint32_t bsp_rtt_get_dropped_full(bsp_rtt_channel_t channel) {
    if (channel != BSP_RTT_LOG && channel != BSP_RTT_DATA) {
        return 0U;
    }
    return dropped_full[channel];
}
