#include "app_crc32.h"
#include "app_flash_journal.h"
#include "flash_export.h"
#include <assert.h>
#include <string.h>

enum { PAGE_COUNT = BSP_FLASH_SIZE_BYTES / BSP_FLASH_PAGE_BYTES };

static const char *control;
static bool rtt_available;
static bool fail_read;
static bool session_fixture, list_fixture;
static unsigned erases, marker_erases, marker_writes;
static unsigned reads;
static enum { NONE, PROBE, READ } flash_op;
static uint32_t *probe_id;
static uint32_t read_index;
static uint8_t *page;
static uint8_t frames[12][276];
static unsigned sizes[12];
static unsigned sent;

static uint32_t get_u32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

size_t bsp_rtt_read_control(void *out, size_t capacity) {
    size_t size = strlen(control);
    if (size > capacity)
        size = capacity;
    memcpy(out, control, size);
    control += size;
    return size;
}

bool bsp_rtt_can_write(bsp_rtt_channel_t channel, size_t size) {
    assert(channel == BSP_RTT_DATA && size <= sizeof(frames[0]));
    return rtt_available;
}

bsp_rtt_result_t bsp_rtt_write(bsp_rtt_channel_t channel, const void *data,
                               size_t size) {
    assert(bsp_rtt_can_write(channel, size));
    assert(sent < sizeof(frames) / sizeof(frames[0]));
    memcpy(frames[sent], data, size);
    sizes[sent++] = size;
    return BSP_RTT_OK;
}

bsp_flash_result_t bsp_flash_probe_start(uint32_t *id) {
    assert(flash_op == NONE);
    flash_op = PROBE;
    probe_id = id;
    return BSP_FLASH_PENDING;
}

bsp_flash_result_t bsp_flash_read_start(uint32_t address, void *out,
                                        size_t size) {
    assert(flash_op == NONE && address < BSP_FLASH_SIZE_BYTES);
    assert(size == BSP_FLASH_PAGE_BYTES && address % size == 0U);
    flash_op = READ;
    read_index = address / BSP_FLASH_PAGE_BYTES;
    page = out;
    ++reads;
    return BSP_FLASH_PENDING;
}

bsp_flash_result_t bsp_flash_program_start(uint32_t address, const void *out,
                                           size_t size) {
    assert(erases == 1U && flash_op == NONE);
    assert(address == BSP_FLASH_SIZE_BYTES - BSP_FLASH_SECTOR_BYTES);
    assert(size == BSP_FLASH_PAGE_BYTES);
    assert(app_flash_journal_is_marker(out, address / BSP_FLASH_PAGE_BYTES));
    ++marker_writes;
    return BSP_FLASH_OK;
}

bsp_flash_result_t bsp_flash_sector_erase_start(uint32_t address) {
    assert(address == BSP_FLASH_SIZE_BYTES - BSP_FLASH_SECTOR_BYTES);
    ++marker_erases;
    return BSP_FLASH_OK;
}

bsp_flash_result_t bsp_flash_chip_erase_start(void) {
    assert(flash_op == NONE);
    ++erases;
    return BSP_FLASH_OK;
}

static void put_u32(uint8_t *bytes, uint32_t value) {
    for (unsigned i = 0; i < 4U; ++i)
        bytes[i] = (uint8_t)(value >> (8U * i));
}

static void make_session_page(void) {
    const bool end = read_index == 12U;
    const unsigned payload_size = end ? 12U : 0U;
    memset(page, 0, 24U + payload_size);
    memcpy(page, "DRJ!", 4);
    page[4] = 1U;
    page[5] = end ? APP_FLASH_JOURNAL_END : APP_FLASH_JOURNAL_BEGIN;
    page[6] = (uint8_t)payload_size;
    put_u32(page + 8, read_index);
    put_u32(page + 12, read_index == 16U ? 16U : 10U);
    if (end)
        put_u32(page + 24, APP_FLASH_JOURNAL_STOPPED);
    put_u32(page + 20,
            app_crc32_extend(app_crc32(page, 20), page + 24, payload_size));
}

bsp_flash_result_t bsp_flash_poll(void) {
    assert(flash_op != NONE);
    const bool reading = flash_op == READ;
    flash_op = NONE;
    if (!reading) {
        *probe_id = BSP_FLASH_JEDEC_ID;
        return BSP_FLASH_OK;
    }
    if (fail_read)
        return BSP_FLASH_SPI_ERROR;
    memset(page, 0xFF, BSP_FLASH_PAGE_BYTES);
    if (session_fixture &&
        (read_index == 10U || read_index == 12U || read_index == 16U))
        make_session_page();
    if (session_fixture && (read_index == 11U || read_index == 15U))
        page[0] =
            0U; /* The bad page between BEGIN and END must not stop export. */
    const uint32_t marker_page =
        PAGE_COUNT - BSP_FLASH_SECTOR_BYTES / BSP_FLASH_PAGE_BYTES;
    if (list_fixture && read_index == marker_page) {
        app_flash_journal_encode_empty_marker(page);
        put_u32(page + 4, 12U);
        put_u32(page + 16, app_crc32(page, 16U));
    } else if (list_fixture && read_index >= 13U && read_index <= 15U) {
        page[0] = 0; /* Damaged pages between otherwise readable sessions. */
    }
    if (marker_erases == 0U &&
        (read_index == 0U || read_index == PAGE_COUNT - 1U))
        page[0] = (uint8_t)read_index ^ 0xA5U;
    return BSP_FLASH_OK;
}

static const uint8_t *check_frame(unsigned n, uint32_t id, uint8_t kind,
                                  uint32_t page_index, unsigned length) {
    assert(n < sent && sizes[n] == 20U + length);
    const uint8_t *frame = frames[n];
    assert(memcmp(frame, "DRE!", 4) == 0 && frame[4] == 1U && frame[5] == kind);
    assert((unsigned)frame[6] + 256U * frame[7] == length);
    assert(get_u32(frame + 8) == id && get_u32(frame + 12) == page_index);
    assert(get_u32(frame + 16 + length) == app_crc32(frame, 16U + length));
    return frame + 16;
}

static void until(flash_export_state_t state) {
    unsigned budget = 2U * PAGE_COUNT + 64U;
    while (flash_export_get_state() != state) {
        assert(budget-- != 0U);
        flash_export_process(100U + (2U * PAGE_COUNT + 64U - budget));
    }
}

int main(void) {
    flash_export_init();
    control = "ERASE!00000022";
    flash_export_process(0U);
    assert(flash_export_get_state() == FLASH_EXPORT_WAIT && erases == 0U);
    control = "EXPORTaa";
    flash_export_process(0U); /* An interrupted command is replaced below. */
    assert(flash_export_get_state() == FLASH_EXPORT_WAIT);

    control = "EXPORT12345678";
    rtt_available = true;
    flash_export_process(0U);
    const uint8_t *begin = check_frame(0, 0x12345678U, 1, 0, 8);
    assert(get_u32(begin) == BSP_FLASH_PAGE_BYTES &&
           get_u32(begin + 4) == PAGE_COUNT);
    rtt_available = false;
    for (unsigned i = 0; i < 20U; ++i)
        flash_export_process(0U);
    assert(reads == 1U && sent == 1U); /* Pending PAGE survives backpressure. */

    rtt_available = true;
    flash_export_process(0U);
    assert(check_frame(1, 0x12345678U, 2, 0, 256)[0] == 0xA5U);
    flash_export_process(0U); /* Read page 1 is in flight when the new request
                               arrives. */
    assert(flash_op == READ);
    control = "EXPORT00000022";
    until(FLASH_EXPORT_DONE);
    assert(reads == PAGE_COUNT + 2U && sent == 6U);
    check_frame(2, 0x22U, 1, 0, 8);
    const uint8_t *first = check_frame(3, 0x22U, 2, 0, 256);
    const uint8_t *last = check_frame(4, 0x22U, 2, PAGE_COUNT - 1U, 256);
    assert(first[0] == 0xA5U && last[0] == 0x5AU);
    const uint8_t *end = check_frame(5, 0x22U, 3, PAGE_COUNT, 12);
    assert(get_u32(end) == 2U && get_u32(end + 4) == 2U);
    uint32_t crc = app_crc32_extend(0U, first - 4, 260);
    crc = app_crc32_extend(crc, last - 4, 260);
    assert(get_u32(end + 8) == crc);

    control = "ERASE!12345678";
    flash_export_process(0U);
    assert(flash_export_get_state() == FLASH_EXPORT_DONE && erases == 0U);

    control = "ERASE!0000";
    flash_export_process(0U);
    assert(flash_export_get_state() == FLASH_EXPORT_DONE && erases == 0U);
    control = "0022";
    rtt_available = false;
    until(FLASH_EXPORT_ERASE_BUSY);
    for (unsigned i = 0; i < 5U; ++i)
        flash_export_process(0U);
    assert(erases == 0U);
    rtt_available = true;
    flash_export_process(0U);
    assert(get_u32(check_frame(6, 0x22U, 5, PAGE_COUNT, 4)) == 0U);
    control = "EXPORT00000044";
    const unsigned before_verify = reads;
    until(FLASH_EXPORT_ERASE_DONE);
    assert(erases == 1U && marker_erases == 1U && marker_writes == 1U);
    assert(reads - before_verify ==
           PAGE_COUNT + BSP_FLASH_SECTOR_BYTES / BSP_FLASH_PAGE_BYTES);
    flash_export_process(0U);
    assert(get_u32(check_frame(7, 0x22U, 5, PAGE_COUNT, 4)) == 1U);
    control = "ERASE!00000022";
    flash_export_process(0U);
    assert(flash_export_get_state() == FLASH_EXPORT_ERASE_DONE && erases == 1U);

    fail_read = true;
    control = "EXPORT00000033";
    until(FLASH_EXPORT_ERROR);
    check_frame(8, 0x33U, 1, 0, 8);
    assert(get_u32(check_frame(9, 0x33U, 4, 0, 4)) == BSP_FLASH_SPI_ERROR);
    flash_export_init();
    fail_read = false;
    session_fixture = true;
    sent = reads = 0;
    control = "EXSESS000000550000000a";
    until(FLASH_EXPORT_DONE);
    assert(reads == 3U && sent == 4U);
    check_frame(0, 0x55U, 1, 0, 8);
    check_frame(1, 0x55U, 2, 10, 256);
    check_frame(2, 0x55U, 2, 12, 256);
    assert(get_u32(check_frame(3, 0x55U, 3, PAGE_COUNT, 12)) == 2U);
    control = "ERASE!00000055";
    flash_export_process(0U);
    assert(flash_export_get_state() == FLASH_EXPORT_DONE && erases == 1U);
    sent = reads = 0;
    control = "EXPREV0000006600000010";
    flash_export_process(0U);
    flash_export_process(0U);
    until(FLASH_EXPORT_DONE);
    assert(reads == 8U && sent == 4U);
    check_frame(1, 0x66U, 2, 10, 256);
    check_frame(2, 0x66U, 2, 12, 256);
    flash_export_init();
    list_fixture = true;
    sent = reads = 0;
    control = "EXPORT00000042";
    assert(!flash_export_poll_boot_request());
    flash_export_process(0U);
    assert(flash_export_get_state() == FLASH_EXPORT_WAIT && sent == 0U);
    control = "ELIS";
    assert(!flash_export_poll_boot_request());
    control = "TS000000";
    assert(!flash_export_poll_boot_request());
    control = "77";
    assert(flash_export_poll_boot_request());
    assert(sent == 0U && reads == 0U);
    until(FLASH_EXPORT_DONE);
    check_frame(0, 0x77U, 1, 0, 8);
    assert(reads < 40U && sent == 4U);
    const uint8_t *interrupted = check_frame(1, 0x77U, 6, 16, 12);
    const uint8_t *complete = check_frame(2, 0x77U, 6, 10, 12);
    assert(get_u32(interrupted) == 16U && get_u32(interrupted + 8U) == 0U);
    assert(get_u32(complete) == 12U && get_u32(complete + 8U) == 1U);
    end = check_frame(3, 0x77U, 3, PAGE_COUNT, 12);
    crc = app_crc32_extend(0U, interrupted - 4U, 16U);
    assert(get_u32(end) == 2U &&
           get_u32(end + 8U) == app_crc32_extend(crc, complete - 4U, 16U));

    flash_export_init();
    sent = reads = 0;
    control = "EXRANG000000880000001000000010";
    until(FLASH_EXPORT_DONE);
    assert(reads == 1U && sent == 3U);
    check_frame(1, 0x88U, 2, 16, 256);
    check_frame(2, 0x88U, 3, PAGE_COUNT, 12);
    control = "ERASE!00000088";
    flash_export_process(0U);
    assert(flash_export_get_state() == FLASH_EXPORT_DONE && erases == 1U);
    return 0;
}
