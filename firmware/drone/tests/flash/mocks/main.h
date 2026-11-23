#ifndef FLASH_TEST_MAIN_H
#define FLASH_TEST_MAIN_H

#include <stdint.h>

#define FLASH_CS_GPIO_Port 1U
#define FLASH_CS_Pin 12U
#define GPIO_PIN_SET 1U
#define GPIO_PIN_RESET 0U

typedef struct {
    unsigned unused;
} SPI_HandleTypeDef;

typedef enum { HAL_OK, HAL_ERROR } HAL_StatusTypeDef;

uint32_t HAL_GetTick(void);
void HAL_GPIO_WritePin(unsigned port, unsigned pin, unsigned state);
HAL_StatusTypeDef HAL_SPI_TransmitReceive_DMA(SPI_HandleTypeDef *spi,
                                              uint8_t *tx, uint8_t *rx,
                                              uint16_t size);
void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *spi);

#endif
