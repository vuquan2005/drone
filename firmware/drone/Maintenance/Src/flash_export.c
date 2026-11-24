#include "flash_export.h"
#include "app_crc32.h"
#include "app_flash_journal.h"
#include "app_log.h"
#include "flash_erase.h"
#include <string.h>

enum {
    PAGE_COUNT = BSP_FLASH_SIZE_BYTES / BSP_FLASH_PAGE_BYTES,
    SCAN_LOG_INTERVAL_PAGES = 4096U,
    HEADER_BYTES = 16U,
    CRC_BYTES = 4U,
    WIRE_VERSION = 1U,
    REQUEST_ID_HEX_BYTES = 8U,
    MESSAGE_BEGIN = 1U,
    MESSAGE_PAGE = 2U,
    MESSAGE_END = 3U,
    MESSAGE_ERROR = 4U,
    MESSAGE_ERASE_STATUS = 5U,
    MESSAGE_SESSION = 6U,
    ERASE_STARTED = 0U,
    ERASE_VERIFIED = 1U,
    ERASE_FAILED = 2U,
};

typedef enum {
    NO_COMMAND,
    EXPORT_COMMAND,
    ERASE_COMMAND,
    SESSION_COMMAND,
    PREVIOUS_COMMAND,
    LIST_COMMAND,
    RANGE_COMMAND
} command_kind_t;

static const char export_prefix[] = "EXPORT";
static const char previous_prefix[] = "EXPREV";
static const char session_prefix[] = "EXSESS";
static const char list_prefix[] = "ELISTS";
static const char range_prefix[] = "EXRANG";
static const char erase_prefix[] = "ERASE!";
enum { PREFIX_BYTES = sizeof(export_prefix) - 1U };
_Static_assert(sizeof(export_prefix) == sizeof(erase_prefix) &&
                   sizeof(export_prefix) == sizeof(session_prefix) &&
                   sizeof(export_prefix) == sizeof(previous_prefix) &&
                   sizeof(export_prefix) == sizeof(list_prefix) &&
                   sizeof(export_prefix) == sizeof(range_prefix),
               "RTT command prefixes must have the same length");

static struct {
    flash_export_state_t state;
    enum { PROBE, READ, RECOVER, LIST_READ, LIST_BEGIN } step;
    enum { FORWARD, CHECK_ANCHOR, BACKWARD, CHECK_PREVIOUS } direction;
    bool pending;
    command_kind_t command_kind;
    command_kind_t request;
    unsigned command_bytes;
    uint32_t command_id;
    uint32_t active_id;
    uint32_t command_session;
    uint32_t command_last;
    uint32_t next_last;
    uint32_t limit;
    uint32_t recovery_progress;
    uint32_t list_begin;
    uint32_t list_last;
    bool list_complete;
    bool can_erase;
    uint32_t next_session;
    uint32_t session;
    bool session_export;
    bool session_finished;
    uint32_t jedec_id;
    uint32_t page;
    uint32_t sent_pages;
    uint32_t invalid_pages;
    uint32_t stream_crc;
    size_t tx_size;
    uint8_t tx_kind;
    uint8_t tx[HEADER_BYTES + BSP_FLASH_PAGE_BYTES + CRC_BYTES];
    uint32_t next_id;
} exporter;

static void put_u32(uint8_t *out, uint32_t value) {
    for (unsigned i = 0U; i < 4U; ++i)
        out[i] = (uint8_t)(value >> (8U * i));
}

static void poll_command(void) {
    uint8_t command[PREFIX_BYTES + REQUEST_ID_HEX_BYTES];
    const size_t count = bsp_rtt_read_control(command, sizeof(command));
    for (size_t i = 0U; i < count; ++i) {
        const uint8_t value = command[i];
        if (exporter.command_bytes < PREFIX_BYTES) {
            if (exporter.command_bytes == 2U && value == 'S' &&
                exporter.command_kind == EXPORT_COMMAND) {
                exporter.command_kind = SESSION_COMMAND;
            }
            if (exporter.command_bytes == 2U && value == 'R' &&
                exporter.command_kind == EXPORT_COMMAND) {
                exporter.command_kind = RANGE_COMMAND;
            }
            if (exporter.command_bytes == 3U && value == 'R' &&
                exporter.command_kind == EXPORT_COMMAND) {
                exporter.command_kind = PREVIOUS_COMMAND;
            }
            if (exporter.command_bytes == 1U) {
                exporter.command_kind = value == 'R'   ? ERASE_COMMAND
                                        : value == 'L' ? LIST_COMMAND
                                                       : EXPORT_COMMAND;
            }
            const char *prefix =
                exporter.command_kind == ERASE_COMMAND      ? erase_prefix
                : exporter.command_kind == SESSION_COMMAND  ? session_prefix
                : exporter.command_kind == PREVIOUS_COMMAND ? previous_prefix
                : exporter.command_kind == LIST_COMMAND     ? list_prefix
                : exporter.command_kind == RANGE_COMMAND    ? range_prefix
                                                            : export_prefix;
            if (value == prefix[exporter.command_bytes]) {
                if (++exporter.command_bytes == PREFIX_BYTES) {
                    exporter.command_id = 0U;
                    exporter.command_session = 0U;
                    exporter.command_last = 0U;
                }
            } else {
                exporter.command_bytes = value == 'E' ? 1U : 0U;
            }
        } else {
            unsigned digit;
            if (value >= '0' && value <= '9') {
                digit = value - '0';
            } else if (value >= 'a' && value <= 'f') {
                digit = value - 'a' + 10U;
            } else {
                exporter.command_bytes = value == 'E' ? 1U : 0U;
                continue;
            }
            const unsigned id_end = PREFIX_BYTES + REQUEST_ID_HEX_BYTES;
            if (exporter.command_bytes < id_end) {
                exporter.command_id = (exporter.command_id << 4U) | digit;
            } else if (exporter.command_bytes < id_end + REQUEST_ID_HEX_BYTES) {
                exporter.command_session =
                    (exporter.command_session << 4U) | digit;
            } else {
                exporter.command_last = (exporter.command_last << 4U) | digit;
            }
            unsigned end = id_end;
            if (exporter.command_kind == RANGE_COMMAND) {
                end += 2U * REQUEST_ID_HEX_BYTES;
            } else if (exporter.command_kind == SESSION_COMMAND ||
                       exporter.command_kind == PREVIOUS_COMMAND) {
                end += REQUEST_ID_HEX_BYTES;
            }
            if (++exporter.command_bytes == end) {
                exporter.command_bytes = 0U;
                exporter.next_id = exporter.command_id;
                exporter.next_session = exporter.command_session;
                exporter.next_last = exporter.command_last;
                exporter.request = exporter.command_kind;
            }
        }
    }
}

static void prepare_message(uint8_t kind, uint16_t size) {
    static const uint8_t magic[] = {'D', 'R', 'E', '!'};
    uint8_t *frame = exporter.tx;
    memcpy(frame, magic, sizeof(magic));
    frame[4] = WIRE_VERSION;
    frame[5] = kind;
    frame[6] = (uint8_t)size;
    frame[7] = (uint8_t)(size >> 8U);
    put_u32(frame + 8, exporter.active_id);
    put_u32(frame + 12, exporter.page);
    const size_t bytes = HEADER_BYTES + size;
    put_u32(frame + bytes, app_crc32(frame, bytes));
    exporter.tx_size = bytes + CRC_BYTES;
    exporter.tx_kind = kind;
}

static void start_export(uint32_t now_ms) {
    const bool listing = exporter.request == LIST_COMMAND;
    const bool range = exporter.request == RANGE_COMMAND;
    exporter.can_erase = exporter.request == EXPORT_COMMAND;
    exporter.limit = PAGE_COUNT;
    exporter.direction =
        exporter.request == PREVIOUS_COMMAND ? CHECK_ANCHOR : FORWARD;
    exporter.session_export = exporter.request == SESSION_COMMAND ||
                              exporter.request == PREVIOUS_COMMAND || range;
    exporter.session_finished = false;
    exporter.session = exporter.next_session;
    exporter.request = NO_COMMAND;
    exporter.active_id = exporter.next_id;
    exporter.step = listing ? RECOVER : PROBE;
    exporter.page = exporter.sent_pages = exporter.invalid_pages = 0U;
    exporter.stream_crc = 0U;
    exporter.jedec_id = 0U;
    exporter.state = FLASH_EXPORT_BUSY;
    uint8_t *payload = exporter.tx + HEADER_BYTES;
    put_u32(payload, BSP_FLASH_PAGE_BYTES);
    put_u32(payload + 4, PAGE_COUNT);
    prepare_message(MESSAGE_BEGIN, 8U);
    if (listing) {
        exporter.recovery_progress = 0U;
        app_flash_journal_init(now_ms);
    }
    if (exporter.session_export) {
        exporter.page = exporter.session;
        if (range && exporter.next_last < PAGE_COUNT) {
            exporter.limit = exporter.next_last + 1U;
        }
        if (exporter.session >= PAGE_COUNT ||
            (range && (exporter.next_last >= PAGE_COUNT ||
                       exporter.session >= exporter.limit))) {
            put_u32(payload, BSP_FLASH_INVALID_ARGUMENT);
            prepare_message(MESSAGE_ERROR, 4U);
        }
    }
    APP_LOG_INFO("flash", "export_start request=%08lx total=%lu",
                 (unsigned long)exporter.active_id, (unsigned long)PAGE_COUNT);
}

static void advance_page(void) {
    ++exporter.page;
    if (exporter.page < PAGE_COUNT &&
        exporter.page % SCAN_LOG_INTERVAL_PAGES == 0U) {
        APP_LOG_INFO(
            "flash", "export_scan request=%08lx scanned=%lu/%lu pages=%lu",
            (unsigned long)exporter.active_id, (unsigned long)exporter.page,
            (unsigned long)PAGE_COUNT, (unsigned long)exporter.sent_pages);
    }
}

static void send_pending(void) {
    if (!bsp_rtt_can_write(BSP_RTT_DATA, exporter.tx_size))
        return;
    if (bsp_rtt_write(BSP_RTT_DATA, exporter.tx, exporter.tx_size) !=
        BSP_RTT_OK)
        return;

    if (exporter.tx_kind == MESSAGE_END) {
        exporter.state = FLASH_EXPORT_DONE;
        APP_LOG_INFO("flash",
                     "export_end request=%08lx pages=%lu invalid_pages=%lu",
                     (unsigned long)exporter.active_id,
                     (unsigned long)exporter.sent_pages,
                     (unsigned long)exporter.invalid_pages);
    } else if (exporter.tx_kind == MESSAGE_ERROR) {
        exporter.state = FLASH_EXPORT_ERROR;
    } else if (exporter.tx_kind == MESSAGE_SESSION) {
        ++exporter.sent_pages;
        if (exporter.list_begin == 0U) {
            exporter.session_finished = true;
        } else {
            exporter.page = exporter.list_begin - 1U;
            exporter.step = LIST_READ;
        }
    } else if (exporter.tx_kind == MESSAGE_PAGE) {
        ++exporter.sent_pages;
        advance_page();
    }
    exporter.tx_size = 0U;
}

static void session_error(void) {
    put_u32(exporter.tx + HEADER_BYTES, BSP_FLASH_INVALID_ARGUMENT);
    prepare_message(MESSAGE_ERROR, 4U);
}

static void previous_page(void) {
    if (exporter.page == 0U) {
        session_error();
    } else {
        --exporter.page;
        exporter.direction = BACKWARD;
    }
}

static void process_page(void) {
    const uint8_t *payload = exporter.tx + HEADER_BYTES;
    app_flash_journal_page_t info;
    const bool valid = app_flash_journal_decode(payload, exporter.page, &info);
    switch (exporter.direction) {
    case CHECK_ANCHOR:
        if (!valid || info.type != APP_FLASH_JOURNAL_BEGIN) {
            session_error();
        } else {
            previous_page();
        }
        return;
    case BACKWARD:
        if (!valid) {
            previous_page();
            return;
        }
        exporter.page = exporter.session = info.session_id;
        exporter.direction = CHECK_PREVIOUS;
        if (info.type != APP_FLASH_JOURNAL_BEGIN) {
            return;
        }
        /* The backward scan landed on the previous BEGIN itself. */
        exporter.direction = FORWARD;
        break;
    case CHECK_PREVIOUS:
        if (!valid || info.type != APP_FLASH_JOURNAL_BEGIN) {
            previous_page();
            return;
        }
        exporter.direction = FORWARD;
        break;
    case FORWARD:
        break;
    }
    if (exporter.session_export) {
        if (exporter.page == exporter.session &&
            (!valid || info.type != APP_FLASH_JOURNAL_BEGIN)) {
            session_error();
            return;
        }
        if (valid && info.session_id != exporter.session) {
            exporter.session_finished = true;
            return;
        }
        if (valid && info.type == APP_FLASH_JOURNAL_END) {
            exporter.session_finished = true;
        }
    }
    size_t i = 0U;
    while (i < BSP_FLASH_PAGE_BYTES && payload[i] == 0xFFU)
        ++i;
    if (i == BSP_FLASH_PAGE_BYTES || (exporter.session_export && !valid)) {
        advance_page();
        return;
    }
    if (!valid && !app_flash_journal_is_marker(payload, exporter.page))
        ++exporter.invalid_pages;
    prepare_message(MESSAGE_PAGE, BSP_FLASH_PAGE_BYTES);
    exporter.stream_crc = app_crc32_extend(
        exporter.stream_crc, exporter.tx + 12, 4U + BSP_FLASH_PAGE_BYTES);
}

static void list_previous(uint32_t index) {
    if (index == 0U) {
        exporter.session_finished = true;
    } else {
        exporter.page = index - 1U;
        exporter.step = LIST_READ;
    }
}

static void list_page(void) {
    uint8_t *payload = exporter.tx + HEADER_BYTES;
    app_flash_journal_page_t info;
    const bool valid = app_flash_journal_decode(payload, exporter.page, &info);
    if (exporter.step == LIST_READ) {
        if (!valid) {
            list_previous(exporter.page);
            return;
        }
        exporter.list_begin = info.session_id;
        exporter.list_last = exporter.page;
        exporter.list_complete = info.type == APP_FLASH_JOURNAL_END;
        if (exporter.page != info.session_id) {
            exporter.page = info.session_id;
            exporter.step = LIST_BEGIN;
            return;
        }
    }
    if (!valid || info.type != APP_FLASH_JOURNAL_BEGIN) {
        list_previous(exporter.page);
        return;
    }
    put_u32(payload, exporter.list_last);
    put_u32(payload + 4, info.uptime_ms);
    put_u32(payload + 8, exporter.list_complete);
    prepare_message(MESSAGE_SESSION, 12U);
    exporter.stream_crc =
        app_crc32_extend(exporter.stream_crc, exporter.tx + 12, 16U);
}

static void recover_list(uint32_t now_ms) {
    app_flash_journal_process(now_ms);
    const app_flash_journal_status_t status = app_flash_journal_status();
    if (status.state == APP_FLASH_JOURNAL_FAULT) {
        put_u32(exporter.tx + HEADER_BYTES, (uint32_t)status.flash_error);
        prepare_message(MESSAGE_ERROR, 4U);
    } else if (status.state != APP_FLASH_JOURNAL_SCANNING) {
        exporter.step = LIST_READ;
        list_previous(status.next_page);
    } else if (status.scanned_pages - exporter.recovery_progress >=
               SCAN_LOG_INTERVAL_PAGES) {
        exporter.recovery_progress = status.scanned_pages;
        APP_LOG_INFO("flash", "list_scan scanned=%lu",
                     (unsigned long)status.scanned_pages);
    }
}

void flash_export_init(void) {
    memset(&exporter, 0, sizeof(exporter));
    exporter.state = FLASH_EXPORT_WAIT;
}

static void erase_status(uint32_t status) {
    put_u32(exporter.tx + HEADER_BYTES, status);
    prepare_message(MESSAGE_ERASE_STATUS, 4U);
}

void flash_export_process(uint32_t now_ms) {
    poll_command();
    if (exporter.state == FLASH_EXPORT_ERASE_BUSY) {
        exporter.request = NO_COMMAND;
        if (exporter.tx_size != 0U) {
            send_pending();
            return;
        }
        flash_erase_process(now_ms);
        const flash_erase_state_t state = flash_erase_get_state();
        if (state == FLASH_ERASE_DONE) {
            exporter.state = FLASH_EXPORT_ERASE_DONE;
            erase_status(ERASE_VERIFIED);
        } else if (state == FLASH_ERASE_ERROR) {
            exporter.state = FLASH_EXPORT_ERASE_ERROR;
            erase_status(ERASE_FAILED);
        }
        return;
    }
    if (exporter.tx_size != 0U && exporter.tx_kind == MESSAGE_ERASE_STATUS) {
        send_pending();
        return;
    }
    if (exporter.request == ERASE_COMMAND) {
        exporter.request = NO_COMMAND;
        if (exporter.state == FLASH_EXPORT_DONE && exporter.can_erase &&
            exporter.next_id == exporter.active_id) {
            flash_erase_start(now_ms);
            exporter.state = FLASH_EXPORT_ERASE_BUSY;
            erase_status(ERASE_STARTED);
            return;
        }
    }
    if (exporter.request == EXPORT_COMMAND ||
        exporter.request == SESSION_COMMAND ||
        exporter.request == PREVIOUS_COMMAND ||
        exporter.request == LIST_COMMAND || exporter.request == RANGE_COMMAND) {
        if (exporter.state == FLASH_EXPORT_BUSY && exporter.step == RECOVER &&
            !app_flash_journal_abort_scan()) {
            return;
        }
        /* Finish any in-flight SPI transfer before reusing the receive buffer.
         */
        if (exporter.pending && bsp_flash_poll() == BSP_FLASH_PENDING)
            return;
        exporter.pending = false;
        start_export(now_ms);
    }
    if (exporter.tx_size != 0U) {
        send_pending();
        return;
    }
    if (exporter.state != FLASH_EXPORT_BUSY)
        return;

    uint8_t *payload = exporter.tx + HEADER_BYTES;
    if (exporter.page >= exporter.limit || exporter.session_finished) {
        put_u32(payload, exporter.sent_pages);
        put_u32(payload + 4, exporter.invalid_pages);
        put_u32(payload + 8, exporter.stream_crc);
        exporter.page = PAGE_COUNT;
        prepare_message(MESSAGE_END, 12U);
        return;
    }

    if (exporter.step == RECOVER) {
        recover_list(now_ms);
        return;
    }
    bsp_flash_result_t result;
    if (exporter.pending) {
        result = bsp_flash_poll();
    } else if (exporter.step == PROBE) {
        result = bsp_flash_probe_start(&exporter.jedec_id);
    } else {
        result = bsp_flash_read_start(exporter.page * BSP_FLASH_PAGE_BYTES,
                                      payload, BSP_FLASH_PAGE_BYTES);
    }
    exporter.pending = result == BSP_FLASH_PENDING;
    if (exporter.pending)
        return;
    if (result != BSP_FLASH_OK) {
        put_u32(payload, (uint32_t)result);
        prepare_message(MESSAGE_ERROR, 4U);
        APP_LOG_ERROR("flash", "export_failed request=%08lx code=%u page=%lu",
                      (unsigned long)exporter.active_id, (unsigned)result,
                      (unsigned long)exporter.page);
    } else if (exporter.step == PROBE) {
        exporter.step = READ;
    } else if (exporter.step == LIST_READ || exporter.step == LIST_BEGIN) {
        list_page();
    } else {
        process_page();
    }
}

flash_export_state_t flash_export_get_state(void) {
    return exporter.state;
}
