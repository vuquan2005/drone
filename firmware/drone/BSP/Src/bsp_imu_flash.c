#include "bsp_imu_flash.h"
#include "stm32f1xx_hal.h"
#include <stdint.h>
#include <string.h>

#define IMU_FLASH_FINALIZED UINT16_C(0xA55A)

extern const uint8_t __imu_flash_start__[];
extern const uint8_t __imu_flash_end__[];

static uintptr_t page_address(unsigned slot, const void *data, size_t size) {
    const uintptr_t start = (uintptr_t)__imu_flash_start__;
    const uintptr_t end = (uintptr_t)__imu_flash_end__;
    if (slot >= BSP_IMU_FLASH_SLOT_COUNT || data == NULL || size == 0U ||
        size % sizeof(uint16_t) != 0U ||
        size > FLASH_PAGE_SIZE - sizeof(uint16_t) ||
        start % FLASH_PAGE_SIZE != 0U ||
        end - start != BSP_IMU_FLASH_SLOT_COUNT * FLASH_PAGE_SIZE ||
        end >
            FLASH_BASE +
                1024U * *(const volatile uint16_t *)FLASH_SIZE_DATA_REGISTER) {
        return 0U;
    }
    return start + slot * FLASH_PAGE_SIZE;
}

static uintptr_t marker_address(uintptr_t page) {
    return page + FLASH_PAGE_SIZE - sizeof(uint16_t);
}

bool bsp_imu_flash_read(unsigned slot, void *data, size_t size) {
    const uintptr_t page = page_address(slot, data, size);
    if (page == 0U || *(const volatile uint16_t *)marker_address(page) !=
                          IMU_FLASH_FINALIZED) {
        return false;
    }
    memcpy(data, (const void *)page, size);
    return true;
}

bool bsp_imu_flash_write(unsigned slot, const void *data, size_t size) {
    const uintptr_t page = page_address(slot, data, size);
    if (page == 0U || !__HAL_RCC_GET_FLAG(RCC_FLAG_HSIRDY) ||
        HAL_FLASH_Unlock() != HAL_OK) {
        return false;
    }

    FLASH_EraseInitTypeDef erase = {
        .TypeErase = FLASH_TYPEERASE_PAGES,
        .PageAddress = (uint32_t)page,
        .NbPages = 1U,
    };
    uint32_t page_error;
    bool ok = HAL_FLASHEx_Erase(&erase, &page_error) == HAL_OK;
    const uint8_t *bytes = data;
    for (size_t offset = 0U; ok && offset < size; offset += sizeof(uint16_t)) {
        uint16_t word;
        memcpy(&word, bytes + offset, sizeof(word));
        if (word != UINT16_MAX) {
            ok = HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD,
                                   (uint32_t)(page + offset), word) == HAL_OK;
        }
    }
    const uintptr_t marker = marker_address(page);
    ok = ok && memcmp((const void *)page, data, size) == 0 &&
         *(const volatile uint16_t *)marker == UINT16_MAX &&
         HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, (uint32_t)marker,
                           IMU_FLASH_FINALIZED) == HAL_OK &&
         *(const volatile uint16_t *)marker == IMU_FLASH_FINALIZED;
    (void)HAL_FLASH_Lock();
    return ok;
}
