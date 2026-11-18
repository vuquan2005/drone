#ifndef SEGGER_RTT_CONF_H
#define SEGGER_RTT_CONF_H

/* Cortex-M3 has no data cache. Use portable C, also exercised by host tests. */
#define RTT_USE_ASM 0
#define SEGGER_RTT_MAX_NUM_UP_BUFFERS 2
#define SEGGER_RTT_MAX_NUM_DOWN_BUFFERS 1
#define BUFFER_SIZE_UP 2048
#define BSP_RTT_DATA_BUFFER_SIZE 4096
#define BUFFER_SIZE_DOWN 16
#define SEGGER_RTT_MODE_DEFAULT SEGGER_RTT_MODE_NO_BLOCK_SKIP

#endif
