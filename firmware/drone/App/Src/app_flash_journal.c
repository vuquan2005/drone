#include "app_flash_journal.h"
#include "app_log.h"
#include <string.h>

enum {
    PAGE_COUNT = BSP_FLASH_SIZE_BYTES / BSP_FLASH_PAGE_BYTES,
    FORMAT_VERSION = 1,
    CRC_OFFSET = 20,
    END_BYTES = 12,
    POWER_UP_MS = 6,
    PAGE_FLUSH_MS = 100,
    FRAME_OVERHEAD = 12,
};

static const uint8_t page_magic[] = {'D', 'R', 'J', '!'};

static struct {
    app_flash_journal_status_t status;
    bool transfer_pending;
    bool close_requested;
    uint32_t powered_ms;
    uint32_t jedec_id;
    uint32_t session_id;
    uint32_t page_ms;
    uint32_t close_ms;
    uint32_t written_frames;
    uint32_t dropped_frames;
    uint16_t used;
    uint16_t frames;
    app_flash_journal_end_reason_t end_reason;
    uint8_t page[BSP_FLASH_PAGE_BYTES];
} journal;

static uint16_t get_u16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t get_u32(const uint8_t *bytes) {
    return (uint32_t)get_u16(bytes) | ((uint32_t)get_u16(bytes + 2) << 16U);
}

static void put_u32(uint8_t *bytes, uint32_t value) {
    for (unsigned i = 0; i < 4U; ++i) {
        bytes[i] = (uint8_t)(value >> (i * 8U));
    }
}

static uint32_t crc_update(uint32_t crc, const uint8_t *bytes, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8U; ++bit) {
            crc = (crc >> 1U) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
        }
    }
    return crc;
}

static uint32_t page_crc(const uint8_t *page, uint16_t size) {
    return crc_update(crc_update(UINT32_MAX, page, CRC_OFFSET),
                      page + APP_FLASH_JOURNAL_HEADER_BYTES, size) ^
           UINT32_MAX;
}

static size_t frame_size(const uint8_t *frame, size_t available) {
    if (available < FRAME_OVERHEAD || memcmp(frame, "DRT!", 4) != 0) {
        return 0;
    }
    const size_t size = FRAME_OVERHEAD + (size_t)get_u16(frame + 6);
    return size <= available ? size : 0;
}

static bool valid_data(const uint8_t *data, size_t size) {
    if (size == 0U) {
        return false;
    }
    while (size != 0U) {
        const size_t length = frame_size(data, size);
        if (length == 0U) {
            return false;
        }
        data += length;
        size -= length;
    }
    return true;
}

bool app_flash_journal_decode(const uint8_t *page, uint32_t index,
                              app_flash_journal_page_t *out) {
    if (page == NULL || out == NULL || index >= PAGE_COUNT ||
        memcmp(page, page_magic, sizeof(page_magic)) != 0 ||
        page[4] != FORMAT_VERSION || get_u32(page + 8) != index) {
        return false;
    }

    app_flash_journal_page_t info = {
        .type = (app_flash_journal_page_type_t)page[5],
        .payload_size = get_u16(page + 6),
        .session_id = get_u32(page + 12),
        .uptime_ms = get_u32(page + 16),
    };
    if (info.payload_size > APP_FLASH_JOURNAL_PAYLOAD_BYTES ||
        get_u32(page + CRC_OFFSET) != page_crc(page, info.payload_size)) {
        return false;
    }

    const uint8_t *payload = page + APP_FLASH_JOURNAL_HEADER_BYTES;
    switch (info.type) {
    case APP_FLASH_JOURNAL_BEGIN:
        if (info.session_id != index || info.payload_size != 0U) {
            return false;
        }
        break;
    case APP_FLASH_JOURNAL_DATA:
        if (info.session_id >= index ||
            !valid_data(payload, info.payload_size)) {
            return false;
        }
        break;
    case APP_FLASH_JOURNAL_END:
        if (info.session_id >= index || info.payload_size != END_BYTES) {
            return false;
        }
        info.end_reason = (app_flash_journal_end_reason_t)get_u32(payload);
        if (info.end_reason != APP_FLASH_JOURNAL_STOPPED &&
            info.end_reason != APP_FLASH_JOURNAL_FILLED) {
            return false;
        }
        info.written_frames = get_u32(payload + 4);
        info.dropped_frames = get_u32(payload + 8);
        break;
    default:
        return false;
    }
    *out = info;
    return true;
}

static bool page_is_erased(const uint8_t *page) {
    for (size_t i = 0; i < BSP_FLASH_PAGE_BYTES; ++i) {
        if (page[i] != 0xFFU) {
            return false;
        }
    }
    return true;
}

static void mark_interrupted(void) {
    if (journal.session_id != UINT32_MAX) {
        ++journal.status.interrupted_sessions;
        journal.session_id = UINT32_MAX;
    }
}

static void idle_after_session(void) {
    journal.status.state = PAGE_COUNT - journal.status.next_page < 4U
                               ? APP_FLASH_JOURNAL_FULL
                               : APP_FLASH_JOURNAL_READY;
    if (journal.status.state == APP_FLASH_JOURNAL_FULL) {
        APP_LOG_WARN("flash", "state=full next_page=%lu free_pages=%lu",
                     (unsigned long)journal.status.next_page,
                     (unsigned long)(PAGE_COUNT - journal.status.next_page));
    }
}

static void scan_page(uint32_t now_ms) {
    (void)now_ms;
    const uint32_t index = journal.status.scanned_pages++;
    if (!page_is_erased(journal.page)) {
        journal.status.next_page = index + 1U;

        app_flash_journal_page_t info;
        if (!app_flash_journal_decode(journal.page, index, &info)) {
            ++journal.status.ignored_pages;
        } else if (info.type == APP_FLASH_JOURNAL_BEGIN) {
            mark_interrupted();
            journal.session_id = info.session_id;
        } else if (info.session_id != journal.session_id) {
            ++journal.status.ignored_pages;
        } else if (info.type == APP_FLASH_JOURNAL_END) {
            journal.session_id = UINT32_MAX;
        }
    }
    if (journal.status.scanned_pages == PAGE_COUNT) {
        mark_interrupted();
        APP_LOG_INFO("telemetry",
                     "scan_done jedec=0x%06lx elapsed_ms=%lu next_page=%lu "
                     "ignored_pages=%lu interrupted_sessions=%lu",
                     (unsigned long)journal.jedec_id,
                     (unsigned long)(now_ms - journal.powered_ms),
                     (unsigned long)journal.status.next_page,
                     (unsigned long)journal.status.ignored_pages,
                     (unsigned long)journal.status.interrupted_sessions);
        idle_after_session();
    }
}

static void seal_page(app_flash_journal_page_type_t type, uint32_t now_ms) {
    memcpy(journal.page, page_magic, sizeof(page_magic));
    journal.page[4] = FORMAT_VERSION;
    journal.page[5] = (uint8_t)type;
    journal.page[6] = (uint8_t)journal.used;
    journal.page[7] = (uint8_t)(journal.used >> 8U);
    put_u32(journal.page + 8, journal.status.next_page);
    put_u32(journal.page + 12, journal.session_id);
    put_u32(journal.page + 16, now_ms);
    put_u32(journal.page + CRC_OFFSET, page_crc(journal.page, journal.used));
    const size_t used_bytes = APP_FLASH_JOURNAL_HEADER_BYTES + journal.used;
    memset(journal.page + used_bytes, 0xFF, sizeof(journal.page) - used_bytes);
}

#if APP_LOG_ENABLED(APP_LOG_LEVEL_ERROR)
static const char *flash_error_name(bsp_flash_result_t result) {
    switch (result) {
    case BSP_FLASH_WRONG_DEVICE:
        return "wrong_device";
    case BSP_FLASH_DEVICE_BUSY:
        return "device_busy";
    case BSP_FLASH_WRITE_PROTECTED:
        return "write_protected";
    case BSP_FLASH_NOT_ERASED:
        return "not_erased";
    case BSP_FLASH_VERIFY_FAILED:
        return "verify_failed";
    case BSP_FLASH_SPI_ERROR:
        return "spi_error";
    case BSP_FLASH_TIMEOUT:
        return "timeout";
    default:
        return "unexpected_result";
    }
}

static void log_transfer_error(bsp_flash_result_t result) {
    const char *error = flash_error_name(result);
    if (journal.status.state == APP_FLASH_JOURNAL_SCANNING &&
        journal.status.scanned_pages == 0U &&
        journal.jedec_id != BSP_FLASH_JEDEC_ID) {
        APP_LOG_ERROR(
            "flash", "op=probe error=%s code=%u jedec=0x%06lx expected=0x%06lx",
            error, (unsigned)result, (unsigned long)journal.jedec_id,
            (unsigned long)BSP_FLASH_JEDEC_ID);
    } else if (journal.status.state == APP_FLASH_JOURNAL_SCANNING) {
        APP_LOG_ERROR("flash", "op=read error=%s code=%u page=%lu addr=0x%06lx",
                      error, (unsigned)result,
                      (unsigned long)journal.status.scanned_pages,
                      (unsigned long)(journal.status.scanned_pages *
                                      BSP_FLASH_PAGE_BYTES));
    } else {
        APP_LOG_ERROR(
            "flash",
            "op=program error=%s code=%u page=%lu addr=0x%06lx session=%lu",
            error, (unsigned)result, (unsigned long)journal.status.next_page,
            (unsigned long)(journal.status.next_page * BSP_FLASH_PAGE_BYTES),
            (unsigned long)journal.session_id);
    }
}
#endif

static bool check_transfer(bsp_flash_result_t result) {
    journal.transfer_pending = result == BSP_FLASH_PENDING;
    if (result != BSP_FLASH_OK && !journal.transfer_pending) {
#if APP_LOG_ENABLED(APP_LOG_LEVEL_ERROR)
        log_transfer_error(result);
#endif
        journal.status.flash_error = result;
        journal.status.state = APP_FLASH_JOURNAL_FAULT;
    }
    return result == BSP_FLASH_OK;
}

static void process_scan(uint32_t now_ms) {
    if (journal.jedec_id != BSP_FLASH_JEDEC_ID) {
        if ((uint32_t)(now_ms - journal.powered_ms) >= POWER_UP_MS) {
            (void)check_transfer(
                journal.transfer_pending
                    ? bsp_flash_poll()
                    : bsp_flash_probe_start(&journal.jedec_id));
        }
        return;
    }

    const uint32_t address =
        journal.status.scanned_pages * BSP_FLASH_PAGE_BYTES;
    const bsp_flash_result_t result =
        journal.transfer_pending
            ? bsp_flash_poll()
            : bsp_flash_read_start(address, journal.page, sizeof(journal.page));
    if (check_transfer(result)) {
        scan_page(now_ms);
    }
}

static void prepare_end(app_flash_journal_end_reason_t reason,
                        uint32_t now_ms) {
    journal.status.state = APP_FLASH_JOURNAL_CLOSING;
    journal.end_reason = reason;
    journal.close_ms = now_ms;
    APP_LOG_INFO("telemetry", "session_closing id=%lu reason=%s",
                 (unsigned long)journal.session_id,
                 reason == APP_FLASH_JOURNAL_FILLED ? "full" : "stopped");
}

static void process_write(app_flash_journal_page_type_t type, uint32_t now_ms) {
    const uint32_t address = journal.status.next_page * BSP_FLASH_PAGE_BYTES;
    if (!journal.transfer_pending) {
        uint32_t timestamp_ms = journal.page_ms;
        if (type == APP_FLASH_JOURNAL_END) {
            uint8_t *payload = journal.page + APP_FLASH_JOURNAL_HEADER_BYTES;
            put_u32(payload, journal.end_reason);
            put_u32(payload + 4, journal.written_frames);
            put_u32(payload + 8, journal.dropped_frames);
            journal.used = END_BYTES;
            timestamp_ms = journal.close_ms;
        }
        seal_page(type, timestamp_ms);
        (void)check_transfer(bsp_flash_program_start(address, journal.page,
                                                     sizeof(journal.page)));
        return;
    }

    if (!check_transfer(bsp_flash_poll())) {
        return;
    }
    ++journal.status.next_page;
    journal.written_frames += journal.frames;
    journal.used = 0;
    journal.frames = 0;

    if (type == APP_FLASH_JOURNAL_BEGIN) {
        APP_LOG_INFO("telemetry", "session_open id=%lu addr=0x%06lx",
                     (unsigned long)journal.session_id, (unsigned long)address);
    }
    if (type == APP_FLASH_JOURNAL_END) {
        APP_LOG_INFO("telemetry",
                     "session_close id=%lu reason=%s written_frames=%lu "
                     "dropped_frames=%lu",
                     (unsigned long)journal.session_id,
                     journal.end_reason == APP_FLASH_JOURNAL_FILLED ? "full"
                                                                    : "stopped",
                     (unsigned long)journal.written_frames,
                     (unsigned long)journal.dropped_frames);
        idle_after_session();
    } else if (PAGE_COUNT - journal.status.next_page == 1U) {
        prepare_end(APP_FLASH_JOURNAL_FILLED, now_ms);
    } else if (journal.close_requested) {
        prepare_end(APP_FLASH_JOURNAL_STOPPED, journal.close_ms);
    } else {
        journal.status.state = APP_FLASH_JOURNAL_OPEN;
    }
}

void app_flash_journal_init(uint32_t now_ms) {
    memset(&journal, 0, sizeof(journal));
    journal.powered_ms = now_ms;
    journal.session_id = UINT32_MAX;
    APP_LOG_INFO("telemetry", "scan_start pages=%lu",
                 (unsigned long)PAGE_COUNT);
}

void app_flash_journal_process(uint32_t now_ms) {
    switch (journal.status.state) {
    case APP_FLASH_JOURNAL_SCANNING:
        process_scan(now_ms);
        break;
    case APP_FLASH_JOURNAL_OPENING:
        process_write(APP_FLASH_JOURNAL_BEGIN, now_ms);
        break;
    case APP_FLASH_JOURNAL_OPEN:
        if (journal.used != 0U &&
            (uint32_t)(now_ms - journal.page_ms) >= PAGE_FLUSH_MS) {
            journal.status.state = APP_FLASH_JOURNAL_WRITING;
        }
        break;
    case APP_FLASH_JOURNAL_WRITING:
        process_write(APP_FLASH_JOURNAL_DATA, now_ms);
        break;
    case APP_FLASH_JOURNAL_CLOSING:
        process_write(APP_FLASH_JOURNAL_END, now_ms);
        break;
    default:
        break;
    }
}

app_flash_journal_status_t app_flash_journal_status(void) {
    return journal.status;
}

bool app_flash_journal_begin(uint32_t now_ms) {
    if (journal.status.state != APP_FLASH_JOURNAL_READY) {
        return false;
    }
    journal.session_id = journal.status.next_page;
    journal.page_ms = now_ms;
    journal.written_frames = 0;
    journal.dropped_frames = 0;
    journal.close_requested = false;
    journal.status.state = APP_FLASH_JOURNAL_OPENING;
    return true;
}

bool app_flash_journal_append(const void *frame, size_t size, uint32_t now_ms) {
    if (journal.status.state != APP_FLASH_JOURNAL_OPEN || frame == NULL ||
        size == 0U || size > APP_FLASH_JOURNAL_PAYLOAD_BYTES ||
        frame_size(frame, size) != size) {
        return false;
    }
    if (size > APP_FLASH_JOURNAL_PAYLOAD_BYTES - journal.used) {
        journal.status.state = APP_FLASH_JOURNAL_WRITING;
        return false;
    }
    if (journal.used == 0U) {
        journal.page_ms = now_ms;
    }
    memcpy(journal.page + APP_FLASH_JOURNAL_HEADER_BYTES + journal.used, frame,
           size);
    journal.used += (uint16_t)size;
    ++journal.frames;
    if (journal.used == APP_FLASH_JOURNAL_PAYLOAD_BYTES) {
        journal.status.state = APP_FLASH_JOURNAL_WRITING;
    }
    return true;
}

static bool accepting(void) {
    return !journal.close_requested &&
           (journal.status.state == APP_FLASH_JOURNAL_OPENING ||
            journal.status.state == APP_FLASH_JOURNAL_OPEN ||
            journal.status.state == APP_FLASH_JOURNAL_WRITING);
}

void app_flash_journal_note_drops(uint32_t count) {
    if (accepting() || (journal.status.state == APP_FLASH_JOURNAL_CLOSING &&
                        journal.end_reason == APP_FLASH_JOURNAL_FILLED &&
                        !journal.transfer_pending)) {
        const uint32_t room = UINT32_MAX - journal.dropped_frames;
        journal.dropped_frames += count < room ? count : room;
    }
}

bool app_flash_journal_close(uint32_t now_ms) {
    if (!accepting()) {
        return false;
    }
    journal.close_requested = true;
    journal.close_ms = now_ms;
    if (journal.status.state == APP_FLASH_JOURNAL_OPEN) {
        if (journal.used == 0U) {
            prepare_end(APP_FLASH_JOURNAL_STOPPED, now_ms);
        } else {
            journal.status.state = APP_FLASH_JOURNAL_WRITING;
        }
    }
    return true;
}
