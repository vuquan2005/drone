#include "bsp_flash.h"
#include "main.h"
#include <stdbool.h>
#include <string.h>

extern SPI_HandleTypeDef hspi2;

enum {
    READ_JEDEC = 0x9F,
    READ_SR1 = 0x05,
    READ_SR2 = 0x35,
    READ_DATA = 0x03,
    WRITE_ENABLE = 0x06,
    PAGE_PROGRAM = 0x02,
    SECTOR_ERASE = 0x20,
    CHIP_ERASE = 0xC7,
    SR1_BUSY = 0x01,
    SR1_WEL = 0x02,
    SR1_BP = 0x1C,
    SR2_CMP = 0x40,
    ADDRESS_HEADER_BYTES = 4,
    DMA_TIMEOUT_MS = 5,
    PROGRAM_TIMEOUT_MS = 5,
    ERASE_POLL_INTERVAL_MS = 50,
};

typedef enum {
    STEP_IDLE,
    STEP_PROBE_READY,
    STEP_PROBE,
    STEP_STATUS,
    STEP_READ_READY,
    STEP_READ,
    STEP_PROGRAM_READY,
    STEP_CHECK_CMP,
    STEP_CHECK_ERASED,
    STEP_WRITE_ENABLE,
    STEP_CHECK_WEL,
    STEP_PROGRAM,
    STEP_WAIT_PROGRAM,
    STEP_CHECK_PROGRAM,
    STEP_VERIFY,
} flash_step_t;

typedef enum {
    DMA_WAIT,
    DMA_DONE,
    DMA_ERROR,
} flash_dma_result_t;

static struct {
    flash_step_t step;
    bsp_flash_result_t fault;
    bool probed;
    enum { NO_ERASE, ERASE_SECTOR, ERASE_CHIP } erase;
    uint32_t transfer_ms;
    uint32_t programmed_ms;
    uint32_t last_poll_ms;
    uint32_t address;
    size_t size;
    void *out;
    uint8_t tx[ADDRESS_HEADER_BYTES + BSP_FLASH_PAGE_BYTES];
    uint8_t rx[ADDRESS_HEADER_BYTES + BSP_FLASH_PAGE_BYTES];
    uint8_t data[BSP_FLASH_PAGE_BYTES];
} flash;

static volatile flash_dma_result_t dma_result;

static void deselect(void) {
    HAL_GPIO_WritePin(FLASH_CS_GPIO_Port, FLASH_CS_Pin, GPIO_PIN_SET);
}

void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *spi) {
    if (spi == &hspi2) {
        deselect();
        dma_result = DMA_DONE;
    }
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *spi) {
    if (spi == &hspi2) {
        deselect();
        dma_result = DMA_ERROR;
    }
}

static bsp_flash_result_t finish(bsp_flash_result_t result) {
    flash.step = STEP_IDLE;
    flash.out = NULL;
    return result;
}

static bsp_flash_result_t fail(bsp_flash_result_t result) {
    /* DMA buffers have static lifetime. Never reuse them after a timeout,
     * including if a completion interrupt arrives after CS was released. */
    deselect();
    flash.fault = result;
    return finish(result);
}

static bsp_flash_result_t start_transfer(flash_step_t next, uint8_t opcode,
                                         uint16_t count) {
    flash.tx[0] = opcode;
    dma_result = DMA_WAIT;
    flash.transfer_ms = HAL_GetTick();
    HAL_GPIO_WritePin(FLASH_CS_GPIO_Port, FLASH_CS_Pin, GPIO_PIN_RESET);
    if (HAL_SPI_TransmitReceive_DMA(&hspi2, flash.tx, flash.rx, count) !=
        HAL_OK) {
        return fail(BSP_FLASH_SPI_ERROR);
    }
    flash.step = next;
    return BSP_FLASH_PENDING;
}

static bsp_flash_result_t start_addressed(flash_step_t next, uint8_t opcode) {
    flash.tx[1] = (uint8_t)(flash.address >> 16U);
    flash.tx[2] = (uint8_t)(flash.address >> 8U);
    flash.tx[3] = (uint8_t)flash.address;
    if (opcode == PAGE_PROGRAM) {
        memcpy(flash.tx + ADDRESS_HEADER_BYTES, flash.data, flash.size);
    } else {
        memset(flash.tx + ADDRESS_HEADER_BYTES, 0, flash.size);
    }
    return start_transfer(next, opcode,
                          (uint16_t)(ADDRESS_HEADER_BYTES + flash.size));
}

static bsp_flash_result_t available(bool require_probe) {
    if (flash.fault != BSP_FLASH_OK) {
        return flash.fault;
    }
    if (flash.step != STEP_IDLE) {
        return BSP_FLASH_OPERATION_ACTIVE;
    }
    if (require_probe && !flash.probed) {
        return BSP_FLASH_NOT_PROBED;
    }
    return BSP_FLASH_OK;
}

static bool valid_range(uint32_t address, size_t size) {
    return address < BSP_FLASH_SIZE_BYTES && size > 0U &&
           size <= BSP_FLASH_PAGE_BYTES &&
           size <= BSP_FLASH_SIZE_BYTES - address;
}

bsp_flash_result_t bsp_flash_probe_start(uint32_t *id) {
    if (id == NULL) {
        return BSP_FLASH_INVALID_ARGUMENT;
    }
    const bsp_flash_result_t result = available(false);
    if (result != BSP_FLASH_OK) {
        return result;
    }
    flash.probed = false;
    flash.out = id;
    return start_transfer(STEP_PROBE_READY, READ_SR1, 2);
}

bsp_flash_result_t bsp_flash_status_start(uint8_t *status) {
    if (status == NULL) {
        return BSP_FLASH_INVALID_ARGUMENT;
    }
    const bsp_flash_result_t result = available(true);
    if (result != BSP_FLASH_OK) {
        return result;
    }
    flash.out = status;
    return start_transfer(STEP_STATUS, READ_SR1, 2);
}

bsp_flash_result_t bsp_flash_read_start(uint32_t address, void *out,
                                        size_t size) {
    if (out == NULL || !valid_range(address, size)) {
        return BSP_FLASH_INVALID_ARGUMENT;
    }
    const bsp_flash_result_t result = available(true);
    if (result != BSP_FLASH_OK) {
        return result;
    }
    flash.address = address;
    flash.size = size;
    flash.out = out;
    return start_transfer(STEP_READ_READY, READ_SR1, 2);
}

bsp_flash_result_t bsp_flash_program_start(uint32_t address, const void *data,
                                           size_t size) {
    if (data == NULL || !valid_range(address, size) ||
        size > BSP_FLASH_PAGE_BYTES - address % BSP_FLASH_PAGE_BYTES) {
        return BSP_FLASH_INVALID_ARGUMENT;
    }
    const bsp_flash_result_t result = available(true);
    if (result != BSP_FLASH_OK) {
        return result;
    }
    flash.address = address;
    flash.size = size;
    flash.erase = NO_ERASE;
    memcpy(flash.data, data, size);
    return start_transfer(STEP_PROGRAM_READY, READ_SR1, 2);
}

bsp_flash_result_t bsp_flash_chip_erase_start(void) {
    const bsp_flash_result_t result = available(true);
    if (result != BSP_FLASH_OK) {
        return result;
    }
    flash.erase = ERASE_CHIP;
    return start_transfer(STEP_PROGRAM_READY, READ_SR1, 2);
}

bsp_flash_result_t bsp_flash_sector_erase_start(uint32_t address) {
    if (address >= BSP_FLASH_SIZE_BYTES ||
        address % BSP_FLASH_SECTOR_BYTES != 0U) {
        return BSP_FLASH_INVALID_ARGUMENT;
    }
    const bsp_flash_result_t result = available(true);
    if (result != BSP_FLASH_OK) {
        return result;
    }
    flash.address = address;
    flash.size = 0U;
    flash.erase = ERASE_SECTOR;
    return start_transfer(STEP_PROGRAM_READY, READ_SR1, 2);
}

static bsp_flash_result_t wait_program(uint32_t now_ms) {
    const uint32_t interval_ms = flash.erase ? ERASE_POLL_INTERVAL_MS : 1U;
    if (now_ms - flash.last_poll_ms < interval_ms) {
        return BSP_FLASH_PENDING;
    }
    flash.last_poll_ms = now_ms;
    return start_transfer(STEP_CHECK_PROGRAM, READ_SR1, 2);
}

bsp_flash_result_t bsp_flash_poll(void) {
    if (flash.step == STEP_IDLE) {
        return flash.fault == BSP_FLASH_OK ? BSP_FLASH_IDLE : flash.fault;
    }

    const uint32_t now_ms = HAL_GetTick();
    if (flash.step == STEP_WAIT_PROGRAM) {
        return wait_program(now_ms);
    }

    const flash_dma_result_t transfer = dma_result;
    if (transfer == DMA_WAIT) {
        return (uint32_t)(now_ms - flash.transfer_ms) < DMA_TIMEOUT_MS
                   ? BSP_FLASH_PENDING
                   : fail(BSP_FLASH_TIMEOUT);
    }
    if (transfer == DMA_ERROR) {
        return fail(BSP_FLASH_SPI_ERROR);
    }

    const uint8_t status = flash.rx[1];
    const uint8_t *payload = flash.rx + ADDRESS_HEADER_BYTES;
    switch (flash.step) {
    case STEP_PROBE_READY:
        /* A missing chip can read as all ones; let JEDEC report the mismatch.
         */
        if (status != 0xFFU && (status & SR1_BUSY) != 0U) {
            return finish(BSP_FLASH_DEVICE_BUSY);
        }
        return start_transfer(STEP_PROBE, READ_JEDEC, 4);
    case STEP_PROBE: {
        const uint32_t id = ((uint32_t)status << 16U) |
                            ((uint32_t)flash.rx[2] << 8U) | flash.rx[3];
        *(uint32_t *)flash.out = id;
        flash.probed = id == BSP_FLASH_JEDEC_ID;
        return finish(flash.probed ? BSP_FLASH_OK : BSP_FLASH_WRONG_DEVICE);
    }
    case STEP_STATUS:
        *(uint8_t *)flash.out = status;
        return finish(BSP_FLASH_OK);
    case STEP_READ_READY:
        if ((status & SR1_BUSY) != 0U) {
            return finish(BSP_FLASH_DEVICE_BUSY);
        }
        return start_addressed(STEP_READ, READ_DATA);
    case STEP_READ:
        memcpy(flash.out, payload, flash.size);
        return finish(BSP_FLASH_OK);
    case STEP_PROGRAM_READY:
        if ((status & SR1_BUSY) != 0U) {
            return finish(BSP_FLASH_DEVICE_BUSY);
        }
        if ((status & SR1_BP) != 0U) {
            return finish(BSP_FLASH_WRITE_PROTECTED);
        }
        return start_transfer(STEP_CHECK_CMP, READ_SR2, 2);
    case STEP_CHECK_CMP:
        if ((status & SR2_CMP) != 0U) {
            return finish(BSP_FLASH_WRITE_PROTECTED);
        }
        return flash.erase ? start_transfer(STEP_WRITE_ENABLE, WRITE_ENABLE, 1)
                           : start_addressed(STEP_CHECK_ERASED, READ_DATA);
    case STEP_CHECK_ERASED:
        for (size_t i = 0; i < flash.size; ++i) {
            if (payload[i] != 0xFFU) {
                return finish(BSP_FLASH_NOT_ERASED);
            }
        }
        return start_transfer(STEP_WRITE_ENABLE, WRITE_ENABLE, 1);
    case STEP_WRITE_ENABLE:
        return start_transfer(STEP_CHECK_WEL, READ_SR1, 2);
    case STEP_CHECK_WEL:
        if ((status & (SR1_BUSY | SR1_WEL)) != SR1_WEL) {
            return finish(BSP_FLASH_WRITE_PROTECTED);
        }
        if (flash.erase == ERASE_CHIP) {
            return start_transfer(STEP_PROGRAM, CHIP_ERASE, 1);
        }
        return start_addressed(STEP_PROGRAM, flash.erase == ERASE_SECTOR
                                                 ? SECTOR_ERASE
                                                 : PAGE_PROGRAM);
    case STEP_PROGRAM:
        flash.programmed_ms = now_ms;
        flash.last_poll_ms = now_ms;
        flash.step = STEP_WAIT_PROGRAM;
        return BSP_FLASH_PENDING;
    case STEP_CHECK_PROGRAM:
        if ((status & SR1_BUSY) == 0U) {
            if (flash.erase) {
                return finish((status & SR1_WEL) == 0U
                                  ? BSP_FLASH_OK
                                  : BSP_FLASH_WRITE_PROTECTED);
            }
            return start_addressed(STEP_VERIFY, READ_DATA);
        }
        const uint32_t timeout_ms =
            flash.erase == ERASE_CHIP     ? BSP_FLASH_CHIP_ERASE_TIMEOUT_MS
            : flash.erase == ERASE_SECTOR ? BSP_FLASH_SECTOR_ERASE_TIMEOUT_MS
                                          : PROGRAM_TIMEOUT_MS;
        if (now_ms - flash.programmed_ms >= timeout_ms) {
            return fail(BSP_FLASH_TIMEOUT);
        }
        flash.step = STEP_WAIT_PROGRAM;
        return BSP_FLASH_PENDING;
    case STEP_VERIFY:
        if (memcmp(payload, flash.data, flash.size) != 0) {
            return fail(BSP_FLASH_VERIFY_FAILED);
        }
        return finish(BSP_FLASH_OK);
    default:
        return fail(BSP_FLASH_SPI_ERROR);
    }
}
