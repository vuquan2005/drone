#include "flash_export.h"
#include "app_crc32.h"
#include "app_flash_journal.h"
#include "app_log.h"
#include <string.h>

enum {
    PAGE_COUNT = BSP_FLASH_SIZE_BYTES / BSP_FLASH_PAGE_BYTES,
    HEADER_BYTES = 16U,
    CRC_BYTES = 4U,
    WIRE_VERSION = 1U,
    REQUEST_ID_HEX_BYTES = 8U,
    MESSAGE_BEGIN = 1U,
    MESSAGE_PAGE = 2U,
    MESSAGE_END = 3U,
    MESSAGE_ERROR = 4U,
};

static const uint8_t command_prefix[] = {'E', 'X', 'P', 'O', 'R', 'T'};

static struct {
    flash_export_state_t state;
    enum { PROBE, READ } step;
    bool pending;
    bool requested;
    unsigned command_bytes;
    uint32_t command_id;
    uint32_t active_id;
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
    uint8_t command[sizeof(command_prefix) + REQUEST_ID_HEX_BYTES];
    const size_t count = bsp_rtt_read_control(command, sizeof(command));
    for (size_t i = 0U; i < count; ++i) {
        const uint8_t value = command[i];
        if (exporter.command_bytes < sizeof(command_prefix)) {
            if (value == command_prefix[exporter.command_bytes]) {
                if (++exporter.command_bytes == sizeof(command_prefix))
                    exporter.command_id = 0U;
            } else {
                exporter.command_bytes = value == command_prefix[0] ? 1U : 0U;
            }
        } else {
            unsigned digit;
            if (value >= '0' && value <= '9') {
                digit = value - '0';
            } else if (value >= 'a' && value <= 'f') {
                digit = value - 'a' + 10U;
            } else {
                exporter.command_bytes = value == command_prefix[0] ? 1U : 0U;
                continue;
            }
            exporter.command_id = (exporter.command_id << 4U) | digit;
            if (++exporter.command_bytes == sizeof(command)) {
                exporter.command_bytes = 0U;
                exporter.next_id = exporter.command_id;
                exporter.requested = true;
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

static void start_export(void) {
    exporter.requested = false;
    exporter.active_id = exporter.next_id;
    exporter.step = PROBE;
    exporter.page = exporter.sent_pages = exporter.invalid_pages = 0U;
    exporter.stream_crc = 0U;
    exporter.jedec_id = 0U;
    exporter.state = FLASH_EXPORT_BUSY;
    uint8_t *payload = exporter.tx + HEADER_BYTES;
    put_u32(payload, BSP_FLASH_PAGE_BYTES);
    put_u32(payload + 4, PAGE_COUNT);
    prepare_message(MESSAGE_BEGIN, 8U);
    APP_LOG_INFO("flash", "export_start");
}

static void send_pending(void) {
    if (!bsp_rtt_can_write(BSP_RTT_DATA, exporter.tx_size))
        return;
    if (bsp_rtt_write(BSP_RTT_DATA, exporter.tx, exporter.tx_size) !=
        BSP_RTT_OK)
        return;

    if (exporter.tx_kind == MESSAGE_END) {
        exporter.state = FLASH_EXPORT_DONE;
    } else if (exporter.tx_kind == MESSAGE_ERROR) {
        exporter.state = FLASH_EXPORT_ERROR;
    } else if (exporter.tx_kind == MESSAGE_PAGE) {
        ++exporter.page;
        ++exporter.sent_pages;
    }
    exporter.tx_size = 0U;
}

static void process_page(void) {
    const uint8_t *payload = exporter.tx + HEADER_BYTES;
    size_t i = 0U;
    while (i < BSP_FLASH_PAGE_BYTES && payload[i] == 0xFFU)
        ++i;
    if (i == BSP_FLASH_PAGE_BYTES) {
        ++exporter.page;
        return;
    }
    app_flash_journal_page_t info;
    if (!app_flash_journal_decode(payload, exporter.page, &info))
        ++exporter.invalid_pages;
    prepare_message(MESSAGE_PAGE, BSP_FLASH_PAGE_BYTES);
    exporter.stream_crc = app_crc32_extend(
        exporter.stream_crc, exporter.tx + 12, 4U + BSP_FLASH_PAGE_BYTES);
}

void flash_export_init(void) {
    memset(&exporter, 0, sizeof(exporter));
    exporter.state = FLASH_EXPORT_WAIT;
}

void flash_export_process(void) {
    poll_command();
    if (exporter.requested) {
        /* Finish any in-flight SPI transfer before reusing the receive buffer.
         */
        if (exporter.pending && bsp_flash_poll() == BSP_FLASH_PENDING)
            return;
        exporter.pending = false;
        start_export();
    }
    if (exporter.tx_size != 0U) {
        send_pending();
        return;
    }
    if (exporter.state != FLASH_EXPORT_BUSY)
        return;

    uint8_t *payload = exporter.tx + HEADER_BYTES;
    if (exporter.page == PAGE_COUNT) {
        put_u32(payload, exporter.sent_pages);
        put_u32(payload + 4, exporter.invalid_pages);
        put_u32(payload + 8, exporter.stream_crc);
        prepare_message(MESSAGE_END, 12U);
        APP_LOG_INFO("flash", "export_end pages=%lu invalid_pages=%lu",
                     (unsigned long)exporter.sent_pages,
                     (unsigned long)exporter.invalid_pages);
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
        APP_LOG_ERROR("flash", "export_failed code=%u page=%lu",
                      (unsigned)result, (unsigned long)exporter.page);
    } else if (exporter.step == PROBE) {
        exporter.step = READ;
    } else {
        process_page();
    }
}

flash_export_state_t flash_export_get_state(void) {
    return exporter.state;
}
