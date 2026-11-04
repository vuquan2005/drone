#ifndef MOCK_MAIN_H
#define MOCK_MAIN_H
#include <stdint.h>
typedef struct {
    uint32_t ErrorCode;
} UART_HandleTypeDef;
typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY } HAL_StatusTypeDef;
#define HAL_UART_ERROR_NONE 0U
#define USART1_IRQn 37
#define __HAL_UART_CLEAR_OREFLAG(uart) ((void)(uart))
uint32_t HAL_GetTick(void);
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t value);
void HAL_NVIC_SetPriority(int irq, int priority, int subpriority);
void HAL_NVIC_EnableIRQ(int irq);
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *uart, uint8_t *byte,
                                      uint16_t length);
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *uart);
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart);
void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart);
#endif
