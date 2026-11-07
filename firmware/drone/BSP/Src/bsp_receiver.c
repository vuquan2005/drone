#include "bsp_receiver.h"
#include "main.h"

extern UART_HandleTypeDef huart1;

#define RX_CAPACITY 256U
#define DMA_CAPACITY 64U

typedef struct {
    uint32_t timestamp_ms;
    uint8_t byte;
} rx_entry_t;

static volatile rx_entry_t queue[RX_CAPACITY];
static volatile uint16_t head;
static volatile uint16_t tail;
static volatile bool discontinuity;
static volatile bool retry_rx;
static uint8_t dma_buffer[DMA_CAPACITY];
static uint16_t dma_position;

static void start_rx(void) {
    dma_position = 0;
    retry_rx = HAL_UARTEx_ReceiveToIdle_DMA(&huart1, dma_buffer,
                                            DMA_CAPACITY) != HAL_OK;
    if (retry_rx) {
        discontinuity = true;
    }
}

bool bsp_receiver_init(void) {
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    (void)HAL_UART_AbortReceive(&huart1);
    __HAL_UART_CLEAR_OREFLAG(&huart1);
    head = tail = 0;
    discontinuity = false;
    HAL_NVIC_SetPriority(USART1_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
    // Both callbacks share producer state and must not preempt each other.
    HAL_NVIC_SetPriority(DMA1_Channel5_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(DMA1_Channel5_IRQn);
    start_rx();
    bool started = !retry_rx;
    __set_PRIMASK(primask);
    return started;
}

bsp_receiver_result_t bsp_receiver_read(uint8_t *byte, uint32_t *timestamp_ms) {
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    if (retry_rx) {
        start_rx();
    }
    bsp_receiver_result_t result = BSP_RECEIVER_EMPTY;
    if (discontinuity) {
        tail = head;
        discontinuity = false;
        result = BSP_RECEIVER_DISCONTINUITY;
    } else if (tail != head) {
        *byte = queue[tail].byte;
        *timestamp_ms = queue[tail].timestamp_ms;
        tail = (uint16_t)((tail + 1U) % RX_CAPACITY);
        result = BSP_RECEIVER_BYTE;
    }
    __set_PRIMASK(primask);
    return result;
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size) {
    (void)size;
    if (huart != &huart1) {
        return;
    }
    if (huart->ErrorCode != HAL_UART_ERROR_NONE || retry_rx) {
        discontinuity = true;
        return;
    }
    // Read the live DMA cursor: an IDLE IRQ can precede a pending HT/TC IRQ.
    // Using the callback's older Size in that case would replay stale bytes.
    uint16_t position =
        (uint16_t)(DMA_CAPACITY - __HAL_DMA_GET_COUNTER(huart->hdmarx));
    position %= DMA_CAPACITY;
    uint32_t now_ms = HAL_GetTick();
    while (dma_position != position) {
        uint16_t next = (uint16_t)((head + 1U) % RX_CAPACITY);
        if (next == tail) {
            discontinuity = true;
        }
        if (!discontinuity) {
            queue[head].byte = dma_buffer[dma_position];
            queue[head].timestamp_ms = now_ms;
            head = next;
        }
        dma_position = (uint16_t)((dma_position + 1U) % DMA_CAPACITY);
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart) {
    if (huart == &huart1) {
        discontinuity = true;
        (void)HAL_UART_AbortReceive(huart);
        __HAL_UART_CLEAR_OREFLAG(huart);
        start_rx();
    }
}
