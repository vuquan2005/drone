#ifndef MOCK_MAIN_H
#define MOCK_MAIN_H
#include <stdint.h>
typedef struct {
    uint16_t remaining;
} DMA_HandleTypeDef;
typedef struct {
    DMA_HandleTypeDef *hdmarx;
    uint32_t ErrorCode;
} UART_HandleTypeDef;
typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY } HAL_StatusTypeDef;
#define HAL_UART_ERROR_NONE 0U
#define USART1_IRQn 37
#define DMA1_Channel5_IRQn 15
#define __HAL_DMA_GET_COUNTER(dma) ((dma)->remaining)
#define __HAL_UART_CLEAR_OREFLAG(uart) ((void)(uart))
uint32_t HAL_GetTick(void);
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t value);
void HAL_NVIC_SetPriority(int irq, int priority, int subpriority);
void HAL_NVIC_EnableIRQ(int irq);
HAL_StatusTypeDef HAL_UARTEx_ReceiveToIdle_DMA(UART_HandleTypeDef *uart,
                                               uint8_t *byte, uint16_t length);
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *uart);
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *uart, uint16_t size);
void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart);
#endif
