#include "flash_erase.h"
#include "app_flash_journal.h"
#include "app_log.h"
#include "app_safety.h"
#include "bsp_flash.h"

enum { CONFIRM_MS = 3000U, PROBE_RETRY_MS = 100U };

typedef enum {
    WAIT_LOW,
    WAIT_HIGH,
    HOLD_HIGH,
    PROBE,
    ERASE_MARKERS,
    VERIFY_MARKERS,
    ERASE,
    VERIFY,
    MARK_EMPTY,
    DONE,
    ERROR,
} erase_step_t;

static struct {
    erase_step_t step;
    bool pending;
    uint32_t hold_ms;
    uint32_t probe_wait_ms;
    uint32_t next_probe_ms;
    uint32_t jedec_id;
    uint32_t address;
    uint8_t page[BSP_FLASH_PAGE_BYTES];
} maintenance;

void flash_erase_rc_init(void) {
    maintenance.step = WAIT_LOW;
    APP_LOG_INFO("flash", "erase_wait");
}

void flash_erase_start(uint32_t now_ms) {
    maintenance.pending = false;
    maintenance.address = 0U;
    maintenance.jedec_id = 0U;
    maintenance.probe_wait_ms = now_ms;
    maintenance.next_probe_ms = now_ms;
    maintenance.step = PROBE;
    APP_LOG_INFO("flash", "erase_confirmed");
}

static void confirm(const app_command_t *command, uint32_t now_ms) {
    if (command == NULL || !command->valid ||
        command->throttle > APP_ARM_THROTTLE_MAX ||
        !command->esc_calibration_requested ||
        command->telemetry_mode != APP_COMMAND_TELEMETRY_STREAM) {
        maintenance.step = WAIT_LOW;
        return;
    }
    if (command->arm_switch_low) {
        maintenance.step = WAIT_HIGH;
        return;
    }
    if (!command->arm_requested) {
        maintenance.step = WAIT_LOW;
        return;
    }
    if (maintenance.step == WAIT_HIGH) {
        maintenance.hold_ms = now_ms;
        maintenance.step = HOLD_HIGH;
    } else if (maintenance.step == HOLD_HIGH &&
               now_ms - maintenance.hold_ms >= CONFIRM_MS) {
        flash_erase_start(now_ms);
    }
}

static bool transfer_completed(bsp_flash_result_t result, const char *op) {
    (void)op;
    maintenance.pending = result == BSP_FLASH_PENDING;
    if (!maintenance.pending && result != BSP_FLASH_OK) {
        maintenance.step = ERROR;
        APP_LOG_ERROR("flash", "op=%s code=%u addr=0x%06lx jedec=0x%06lx", op,
                      (unsigned)result, (unsigned long)maintenance.address,
                      (unsigned long)maintenance.jedec_id);
    }
    return result == BSP_FLASH_OK;
}

static void probe(uint32_t now_ms) {
    if (!maintenance.pending &&
        (int32_t)(now_ms - maintenance.next_probe_ms) < 0) {
        return;
    }
    const bsp_flash_result_t result =
        maintenance.pending ? bsp_flash_poll()
                            : bsp_flash_probe_start(&maintenance.jedec_id);
    if (result == BSP_FLASH_DEVICE_BUSY &&
        now_ms - maintenance.probe_wait_ms < BSP_FLASH_CHIP_ERASE_TIMEOUT_MS) {
        maintenance.pending = false;
        maintenance.next_probe_ms = now_ms + PROBE_RETRY_MS;
        return;
    }
    if (transfer_completed(result, "erase_probe")) {
        /* Clear old checkpoints before chip erase can leave holes in data. */
        maintenance.address = BSP_FLASH_SIZE_BYTES - BSP_FLASH_SECTOR_BYTES;
        maintenance.step = ERASE_MARKERS;
        APP_LOG_INFO("flash", "erase_start jedec=0x%06lx",
                     (unsigned long)maintenance.jedec_id);
    }
}

static bool verify_page(void) {
    const bsp_flash_result_t result =
        maintenance.pending
            ? bsp_flash_poll()
            : bsp_flash_read_start(maintenance.address, maintenance.page,
                                   sizeof(maintenance.page));
    if (!transfer_completed(result, "erase_verify_read")) {
        return false;
    }
    for (size_t i = 0U; i < sizeof(maintenance.page); ++i) {
        if (maintenance.page[i] != 0xFFU) {
            maintenance.address += i;
            (void)transfer_completed(BSP_FLASH_NOT_ERASED, "erase_verify");
            return false;
        }
    }
    maintenance.address += sizeof(maintenance.page);
    return maintenance.address == BSP_FLASH_SIZE_BYTES;
}

void flash_erase_rc_process(const app_command_t *command, uint32_t now_ms) {
    if (flash_erase_get_state() == FLASH_ERASE_WAIT) {
        confirm(command, now_ms);
        return;
    }
    flash_erase_process(now_ms);
}

void flash_erase_process(uint32_t now_ms) {
    switch (maintenance.step) {
    case WAIT_LOW:
    case WAIT_HIGH:
    case HOLD_HIGH:
        break;
    case PROBE:
        probe(now_ms);
        break;
    case ERASE_MARKERS:
        if (transfer_completed(
                maintenance.pending
                    ? bsp_flash_poll()
                    : bsp_flash_sector_erase_start(maintenance.address),
                "erase_markers")) {
            maintenance.step = VERIFY_MARKERS;
        }
        break;
    case VERIFY_MARKERS:
        if (verify_page()) {
            maintenance.address = 0U;
            maintenance.step = ERASE;
        }
        break;
    case ERASE:
        if (transfer_completed(maintenance.pending
                                   ? bsp_flash_poll()
                                   : bsp_flash_chip_erase_start(),
                               "chip_erase")) {
            maintenance.step = VERIFY;
            APP_LOG_INFO("flash", "erase_verify_start");
        }
        break;
    case VERIFY:
        if (verify_page()) {
            maintenance.address =
                app_flash_journal_encode_empty_marker(maintenance.page);
            maintenance.step = MARK_EMPTY;
        }
        break;
    case MARK_EMPTY:
        if (transfer_completed(maintenance.pending
                                   ? bsp_flash_poll()
                                   : bsp_flash_program_start(
                                         maintenance.address, maintenance.page,
                                         sizeof(maintenance.page)),
                               "erase_mark_empty")) {
            maintenance.step = DONE;
            APP_LOG_INFO("flash", "erase_done bytes=%lu",
                         (unsigned long)BSP_FLASH_SIZE_BYTES);
        }
        break;
    case DONE:
    case ERROR:
        break;
    }
}

flash_erase_state_t flash_erase_get_state(void) {
    switch (maintenance.step) {
    case WAIT_LOW:
    case WAIT_HIGH:
    case HOLD_HIGH:
        return FLASH_ERASE_WAIT;
    case DONE:
        return FLASH_ERASE_DONE;
    case ERROR:
        return FLASH_ERASE_ERROR;
    default:
        return FLASH_ERASE_BUSY;
    }
}
