#include "bsp_receiver.h"
#include "main.h"

extern UART_HandleTypeDef huart1;

#define RX_CAPACITY 256U

typedef struct {
    uint32_t timestamp_ms;
    uint8_t byte;
} rx_entry_t;

static volatile rx_entry_t queue[RX_CAPACITY];
static volatile uint16_t head;
static volatile uint16_t tail;
static volatile bool discontinuity;
static volatile bool retry_rx;
static uint8_t rx_byte;

static void start_rx(void) {
    retry_rx = HAL_UART_Receive_IT(&huart1, &rx_byte, 1) != HAL_OK;
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

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart != &huart1) {
        return;
    }
    // HAL may invoke this callback before its error callback. Do not clear
    // ErrorCode by rearming here: HAL still needs it to handle ORE/FE/NE.
    if (huart->ErrorCode != HAL_UART_ERROR_NONE) {
        discontinuity = true;
        return;
    }
    uint8_t byte = rx_byte;
    uint32_t now_ms = HAL_GetTick();
    start_rx();
    uint16_t next = (uint16_t)((head + 1U) % RX_CAPACITY);
    if (next == tail) {
        discontinuity = true;
    }
    if (!discontinuity) {
        queue[head].byte = byte;
        queue[head].timestamp_ms = now_ms;
        head = next;
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
