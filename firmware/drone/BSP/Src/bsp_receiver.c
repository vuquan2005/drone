#include "bsp_receiver.h"
#include "main.h"
#include <string.h>

/* Board mapping: keep in sync with drone.ioc and CubeMX IRQ handlers.
 * Pin labels RECEIVER_TX/RX describe the STM32 side of the connection.
 * Both IRQs share producer state and must use the same preemption priority. */
#define RECEIVER_UART_HANDLE huart1
#define RECEIVER_UART_IRQn USART1_IRQn
#define RECEIVER_RX_DMA_IRQn DMA1_Channel5_IRQn
#define RECEIVER_IRQ_PRIORITY 5U

/* Owned and initialized by CubeMX before bsp_receiver_init(). */
extern UART_HandleTypeDef RECEIVER_UART_HANDLE;

#define RX_CAPACITY 256U
#define DMA_CAPACITY 64U
#define TX_CAPACITY 64U

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
static uint8_t tx_buffer[TX_CAPACITY];

static void start_rx(void) {
    dma_position = 0;
    retry_rx = HAL_UARTEx_ReceiveToIdle_DMA(&RECEIVER_UART_HANDLE, dma_buffer,
                                            DMA_CAPACITY) != HAL_OK;
    if (retry_rx) {
        discontinuity = true;
    }
}

bool bsp_receiver_init(void) {
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    (void)HAL_UART_AbortReceive(&RECEIVER_UART_HANDLE);
    __HAL_UART_CLEAR_OREFLAG(&RECEIVER_UART_HANDLE);
    head = tail = 0;
    discontinuity = false;
    HAL_NVIC_SetPriority(RECEIVER_UART_IRQn, RECEIVER_IRQ_PRIORITY, 0);
    HAL_NVIC_EnableIRQ(RECEIVER_UART_IRQn);
    // Both callbacks share producer state and must not preempt each other.
    HAL_NVIC_SetPriority(RECEIVER_RX_DMA_IRQn, RECEIVER_IRQ_PRIORITY, 0);
    HAL_NVIC_EnableIRQ(RECEIVER_RX_DMA_IRQn);
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

bool bsp_receiver_try_send(const uint8_t *frame, uint8_t length) {
    if (frame == NULL || length == 0U || length > sizeof(tx_buffer))
        return false;

    UART_HandleTypeDef *uart = &RECEIVER_UART_HANDLE;
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    bool started = false;
    /* HAL_UART_GetState also includes RX DMA state. HAL_UART_Transmit_IT
     * clears ErrorCode, so let a pending RX error finish before starting TX. */
    if (uart->gState == HAL_UART_STATE_READY &&
        uart->ErrorCode == HAL_UART_ERROR_NONE) {
        memcpy(tx_buffer, frame, length);
        started = HAL_UART_Transmit_IT(uart, tx_buffer, length) == HAL_OK;
    }
    __set_PRIMASK(primask);
    return started;
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size) {
    (void)size;
    if (huart != &RECEIVER_UART_HANDLE) {
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
    if (huart == &RECEIVER_UART_HANDLE) {
        discontinuity = true;
        (void)HAL_UART_AbortReceive(huart);
        __HAL_UART_CLEAR_OREFLAG(huart);
        start_rx();
    }
}
