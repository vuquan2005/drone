#include "bsp_flash.h"
#include "main.h"
#include <assert.h>
#include <stdbool.h>
#include <string.h>

SPI_HandleTypeDef hspi2;
static uint32_t tick_ms;
static bool selected;
static uint8_t sr1, sr2;
static unsigned commands, chip_erases;

uint32_t HAL_GetTick(void) {
    return tick_ms;
}

void HAL_GPIO_WritePin(unsigned port, unsigned pin, unsigned state) {
    assert(port == FLASH_CS_GPIO_Port && pin == FLASH_CS_Pin);
    selected = state == GPIO_PIN_RESET;
}

HAL_StatusTypeDef HAL_SPI_TransmitReceive_DMA(SPI_HandleTypeDef *spi,
                                              uint8_t *tx, uint8_t *rx,
                                              uint16_t size) {
    assert(spi == &hspi2 && selected);
    ++commands;
    memset(rx, 0, size);
    switch (tx[0]) {
    case 0x05:
        assert(size == 2U);
        rx[1] = sr1;
        break;
    case 0x35:
        assert(size == 2U);
        rx[1] = sr2;
        break;
    case 0x9F:
        assert(size == 4U);
        rx[1] = 0xEF;
        rx[2] = 0x40;
        rx[3] = 0x18;
        break;
    case 0x06:
        assert(size == 1U && sr1 == 0U);
        sr1 = 2U;
        break;
    case 0xC7:
        assert(size == 1U && sr1 == 2U);
        sr1 = 3U;
        ++chip_erases;
        break;
    default:
        assert(false);
    }
    HAL_SPI_TxRxCpltCallback(spi);
    assert(!selected);
    return HAL_OK;
}

static void begin_erase(void) {
    assert(bsp_flash_chip_erase_start() == BSP_FLASH_PENDING);
    for (unsigned i = 0; i < 5U; ++i)
        assert(bsp_flash_poll() == BSP_FLASH_PENDING);
}

int main(void) {
    uint32_t id = 0U;
    assert(bsp_flash_chip_erase_start() == BSP_FLASH_NOT_PROBED);
    sr1 = 1U;
    assert(bsp_flash_probe_start(&id) == BSP_FLASH_PENDING);
    assert(bsp_flash_poll() == BSP_FLASH_DEVICE_BUSY && id == 0U);
    sr1 = 0U;
    assert(bsp_flash_probe_start(&id) == BSP_FLASH_PENDING);
    assert(bsp_flash_poll() == BSP_FLASH_PENDING);
    assert(bsp_flash_poll() == BSP_FLASH_OK && id == BSP_FLASH_JEDEC_ID);

    sr2 = 0x40U;
    assert(bsp_flash_chip_erase_start() == BSP_FLASH_PENDING);
    assert(bsp_flash_poll() == BSP_FLASH_PENDING);
    assert(bsp_flash_poll() == BSP_FLASH_WRITE_PROTECTED && chip_erases == 0U);
    sr2 = 0U;

    begin_erase();
    assert(chip_erases == 1U);
    assert(bsp_flash_probe_start(&id) == BSP_FLASH_OPERATION_ACTIVE);
    const unsigned before_wait = commands;
    tick_ms += 49U;
    assert(bsp_flash_poll() == BSP_FLASH_PENDING && commands == before_wait);
    ++tick_ms;
    assert(bsp_flash_poll() == BSP_FLASH_PENDING &&
           commands == before_wait + 1U);
    assert(bsp_flash_poll() == BSP_FLASH_PENDING);
    sr1 = 0U;
    tick_ms += 50U;
    assert(bsp_flash_poll() == BSP_FLASH_PENDING);
    assert(bsp_flash_poll() == BSP_FLASH_OK);
    assert(bsp_flash_poll() == BSP_FLASH_IDLE);

    begin_erase();
    tick_ms += BSP_FLASH_CHIP_ERASE_TIMEOUT_MS;
    assert(bsp_flash_poll() == BSP_FLASH_PENDING);
    assert(bsp_flash_poll() == BSP_FLASH_TIMEOUT && chip_erases == 2U);
    assert(bsp_flash_chip_erase_start() == BSP_FLASH_TIMEOUT);
    return 0;
}
