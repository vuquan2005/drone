#ifndef MOCK_STM32F1XX_HAL_H
#define MOCK_STM32F1XX_HAL_H

#include <stdint.h>

typedef struct {
    int unused;
} SPI_HandleTypeDef;
typedef struct {
    int unused;
} GPIO_TypeDef;
typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef enum { GPIO_PIN_RESET, GPIO_PIN_SET } GPIO_PinState;

void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state);
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *spi, uint8_t *tx,
                                          uint8_t *rx, uint16_t length,
                                          uint32_t timeout);
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *spi, uint8_t *tx,
                                   uint16_t length, uint32_t timeout);

#endif
