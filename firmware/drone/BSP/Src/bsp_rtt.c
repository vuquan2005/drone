#include "bsp_rtt.h"
#include "SEGGER_RTT.h"

static char data_buffer[BSP_RTT_DATA_BUFFER_SIZE];
static bool initialized;

void bsp_rtt_init(void) {
    SEGGER_RTT_Init();
    initialized = SEGGER_RTT_ConfigUpBuffer(BSP_RTT_DATA, "Data", data_buffer,
                                            sizeof(data_buffer),
                                            SEGGER_RTT_MODE_NO_BLOCK_SKIP) == 0;
}

bool bsp_rtt_write(bsp_rtt_channel_t channel, const void *data, size_t size) {
    if (!initialized || (channel != BSP_RTT_LOG && channel != BSP_RTT_DATA) ||
        (data == NULL && size != 0U)) {
        return false;
    }
    size_t capacity =
        channel == BSP_RTT_LOG ? BUFFER_SIZE_UP : sizeof(data_buffer);
    if (size >= capacity) {
        return false;
    }
    if (size == 0U) {
        return true;
    }
    /* One foreground writer: avoid masking RX interrupts during the copy. */
    return SEGGER_RTT_WriteNoLock((unsigned)channel, data, (unsigned)size) ==
           size;
}
