#include "bsp_flash.h"

typedef enum {
    FLASH_SMOKE_IDLE,
    FLASH_SMOKE_PROBE,
    FLASH_SMOKE_READ,
    FLASH_SMOKE_PROGRAM,
} flash_smoke_command_t;

/* Set address and command through SWD while the CPU is halted. */
static volatile flash_smoke_command_t flash_smoke_command;
static volatile uint32_t flash_smoke_address = BSP_FLASH_SIZE_BYTES;
static volatile bsp_flash_result_t flash_smoke_result;
static uint32_t flash_smoke_id;
static uint8_t flash_smoke_data[BSP_FLASH_PAGE_BYTES];

static bsp_flash_result_t run_command(flash_smoke_command_t command) {
    if (command == FLASH_SMOKE_PROBE) {
        return bsp_flash_probe_start(&flash_smoke_id);
    }

    const uint32_t address = flash_smoke_address;
    if (address % BSP_FLASH_PAGE_BYTES != 0U) {
        return BSP_FLASH_INVALID_ARGUMENT;
    }
    if (command == FLASH_SMOKE_READ) {
        return bsp_flash_read_start(address, flash_smoke_data,
                                    sizeof(flash_smoke_data));
    }
    if (command == FLASH_SMOKE_PROGRAM) {
        for (size_t i = 0; i < sizeof(flash_smoke_data); ++i) {
            flash_smoke_data[i] = (uint8_t)(i ^ 0xA5U);
        }
        return bsp_flash_program_start(address, flash_smoke_data,
                                       sizeof(flash_smoke_data));
    }
    return BSP_FLASH_INVALID_ARGUMENT;
}

_Noreturn void flash_board_smoke_run(void) {
    flash_smoke_result = run_command(FLASH_SMOKE_PROBE);

    for (;;) {
        if (flash_smoke_result == BSP_FLASH_PENDING) {
            flash_smoke_result = bsp_flash_poll();
        } else if (flash_smoke_command != FLASH_SMOKE_IDLE) {
            const flash_smoke_command_t command = flash_smoke_command;
            flash_smoke_command = FLASH_SMOKE_IDLE;
            flash_smoke_result = run_command(command);
        }
    }
}
