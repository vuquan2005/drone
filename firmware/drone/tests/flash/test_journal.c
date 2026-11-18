#include "app_flash_journal.h"
#include <assert.h>
#include <string.h>

enum { PAGE_COUNT = BSP_FLASH_SIZE_BYTES / BSP_FLASH_PAGE_BYTES };

static uint8_t storage[PAGE_COUNT][BSP_FLASH_PAGE_BYTES];
static uint8_t pending_page[BSP_FLASH_PAGE_BYTES];
static enum { IDLE, PROBING, READING, PROGRAMMING } operation;
static uint32_t *probe_out;
static void *read_out;
static uint32_t index_pending;
static uint32_t now_ms;
static uint32_t programs;
static bsp_flash_result_t next_error;
static const uint8_t imu_frame[44] = {
    'D', 'R', 'T',         '!',         1,           4,
    32,  0,   [40] = 0x4d, [41] = 0xaa, [42] = 0x39, [43] = 0x7b};
static const uint8_t large_frame[232] = {
    'D', 'R',          'T',          '!',          1,           9, 220,
    0,   [228] = 0xb1, [229] = 0x99, [230] = 0x8c, [231] = 0x5a};

bsp_flash_result_t bsp_flash_probe_start(uint32_t *id) {
    assert(operation == IDLE);
    operation = PROBING;
    probe_out = id;
    return BSP_FLASH_PENDING;
}

static void check_address(uint32_t address, size_t size) {
    assert(operation == IDLE);
    assert(address < BSP_FLASH_SIZE_BYTES);
    assert(address % BSP_FLASH_PAGE_BYTES == 0);
    assert(size == BSP_FLASH_PAGE_BYTES);
    index_pending = address / BSP_FLASH_PAGE_BYTES;
}

bsp_flash_result_t bsp_flash_read_start(uint32_t address, void *out,
                                        size_t size) {
    check_address(address, size);
    operation = READING;
    read_out = out;
    return BSP_FLASH_PENDING;
}

bsp_flash_result_t bsp_flash_program_start(uint32_t address, const void *data,
                                           size_t size) {
    check_address(address, size);
    for (size_t i = 0; i < size; ++i) {
        assert(storage[index_pending][i] == 0xFFU);
    }
    operation = PROGRAMMING;
    memcpy(pending_page, data, size);
    return BSP_FLASH_PENDING;
}

bsp_flash_result_t bsp_flash_poll(void) {
    if (next_error != BSP_FLASH_OK) {
        const bsp_flash_result_t error = next_error;
        next_error = BSP_FLASH_OK;
        operation = IDLE;
        return error;
    }
    switch (operation) {
    case PROBING:
        *probe_out = BSP_FLASH_JEDEC_ID;
        break;
    case READING:
        memcpy(read_out, storage[index_pending], BSP_FLASH_PAGE_BYTES);
        break;
    case PROGRAMMING:
        memcpy(storage[index_pending], pending_page, BSP_FLASH_PAGE_BYTES);
        ++programs;
        break;
    default:
        assert(false);
    }
    operation = IDLE;
    return BSP_FLASH_OK;
}

static app_flash_journal_state_t state(void) {
    return app_flash_journal_status().state;
}

static void process(void) {
    app_flash_journal_process(now_ms);
}

static void until(app_flash_journal_state_t target) {
    uint32_t budget = 2 * PAGE_COUNT + 12;
    while (state() != target) {
        assert(budget-- != 0U);
        process();
    }
}

static void reboot(void) {
    operation = IDLE;
    app_flash_journal_init(now_ms);
    process();
    assert(operation == IDLE);
    now_ms += 6;
}

static void clean_chip(void) {
    memset(storage, 0xFF, sizeof(storage));
    programs = 0;
    next_error = BSP_FLASH_OK;
    now_ms = 0;
}

static app_flash_journal_page_t decode(uint32_t index,
                                       app_flash_journal_page_type_t type) {
    app_flash_journal_page_t page;
    assert(app_flash_journal_decode(storage[index], index, &page));
    assert(page.type == type);
    return page;
}

static void test_append_and_flush(void) {
    clean_chip();
    reboot();
    until(APP_FLASH_JOURNAL_READY);
    assert(app_flash_journal_status().scanned_pages == PAGE_COUNT);
    assert(app_flash_journal_begin(100));
    assert(!app_flash_journal_append(imu_frame, sizeof(imu_frame), 100));
    until(APP_FLASH_JOURNAL_OPEN);
    assert(decode(0, APP_FLASH_JOURNAL_BEGIN).uptime_ms == 100);
    const uint8_t expected_begin[] = {
        0x44, 0x52, 0x4a, 0x21, 1,   1, 0, 0, 0,    0,    0,    0,
        0,    0,    0,    0,    100, 0, 0, 0, 0x6a, 0x67, 0xe4, 0xFC,
    };
    assert(memcmp(storage[0], expected_begin, sizeof(expected_begin)) == 0);

    for (unsigned i = 0; i < 5; ++i) {
        assert(app_flash_journal_append(imu_frame, sizeof(imu_frame), 110 + i));
    }
    assert(!app_flash_journal_append(imu_frame, sizeof(imu_frame), 115));
    assert(state() == APP_FLASH_JOURNAL_WRITING);
    until(APP_FLASH_JOURNAL_OPEN);
    const app_flash_journal_page_t data = decode(1, APP_FLASH_JOURNAL_DATA);
    assert(data.payload_size == 5 * sizeof(imu_frame) && data.uptime_ms == 110);
    for (unsigned i = 0; i < 5; ++i) {
        const size_t offset =
            APP_FLASH_JOURNAL_HEADER_BYTES + i * sizeof(imu_frame);
        assert(memcmp(storage[1] + offset, imu_frame, sizeof(imu_frame)) == 0);
    }
    assert(storage[1][BSP_FLASH_PAGE_BYTES - 1] == 0xFF);
    assert(!app_flash_journal_append(imu_frame, sizeof(imu_frame) - 1, 115));

    now_ms = UINT32_MAX - 40;
    assert(app_flash_journal_append(imu_frame, sizeof(imu_frame), now_ms));
    now_ms += 99;
    process();
    assert(state() == APP_FLASH_JOURNAL_OPEN);
    ++now_ms;
    process();
    assert(state() == APP_FLASH_JOURNAL_WRITING);
    app_flash_journal_note_drop();
    assert(app_flash_journal_close(now_ms));
    assert(!app_flash_journal_close(now_ms));
    until(APP_FLASH_JOURNAL_READY);
    const app_flash_journal_page_t end = decode(3, APP_FLASH_JOURNAL_END);
    assert(end.uptime_ms == now_ms && end.written_frames == 6);
    assert(end.dropped_frames == 1 &&
           end.end_reason == APP_FLASH_JOURNAL_STOPPED);

    reboot();
    until(APP_FLASH_JOURNAL_READY);
    const app_flash_journal_status_t status = app_flash_journal_status();
    assert(status.next_page == 4 && status.interrupted_sessions == 0);
}

static void cut_power(size_t programmed_bytes) {
    assert(operation == PROGRAMMING);
    memcpy(storage[index_pending], pending_page, programmed_bytes);
    reboot();
    until(APP_FLASH_JOURNAL_READY);
}

static void test_recovery(void) {
    clean_chip();
    /* An unrelated chip may have a hole and a torn page with only payload bits.
     */
    storage[0][0] = 0;
    storage[3][120] = 0;
    reboot();
    until(APP_FLASH_JOURNAL_READY);
    assert(app_flash_journal_status().next_page == 4);
    assert(app_flash_journal_status().ignored_pages == 2);

    assert(app_flash_journal_begin(now_ms));
    process();
    cut_power(8); /* BEGIN CRC never landed. */
    assert(app_flash_journal_status().next_page == 5);
    assert(app_flash_journal_status().ignored_pages == 3);
    assert(app_flash_journal_status().interrupted_sessions == 0);

    assert(app_flash_journal_begin(now_ms));
    until(APP_FLASH_JOURNAL_OPEN);
    assert(app_flash_journal_append(large_frame, sizeof(large_frame), now_ms));
    until(APP_FLASH_JOURNAL_OPEN);
    assert(app_flash_journal_append(large_frame, sizeof(large_frame), now_ms));
    process();
    cut_power(80); /* A valid earlier DATA page remains readable. */
    assert(decode(6, APP_FLASH_JOURNAL_DATA).session_id == 5);
    assert(app_flash_journal_status().next_page == 8);
    assert(app_flash_journal_status().ignored_pages == 4);
    assert(app_flash_journal_status().interrupted_sessions == 1);

    assert(app_flash_journal_begin(now_ms));
    assert(app_flash_journal_close(now_ms));
    until(APP_FLASH_JOURNAL_CLOSING);
    process();
    cut_power(APP_FLASH_JOURNAL_HEADER_BYTES + 1);
    assert(app_flash_journal_status().next_page == 10);
    assert(app_flash_journal_status().ignored_pages == 5);
    assert(app_flash_journal_status().interrupted_sessions == 2);
    assert(storage[0][0] == 0 && storage[3][120] == 0);

    storage[5][20] ^= 1U; /* BEGIN damaged, its DATA must count as orphaned. */
    reboot();
    until(APP_FLASH_JOURNAL_READY);
    assert(app_flash_journal_status().ignored_pages == 7);
    assert(app_flash_journal_status().interrupted_sessions == 1);
}

static void test_full(void) {
    clean_chip();
    storage[PAGE_COUNT - 4][0] = 0;
    reboot();
    until(APP_FLASH_JOURNAL_READY);
    assert(app_flash_journal_begin(now_ms));
    until(APP_FLASH_JOURNAL_OPEN);
    assert(app_flash_journal_append(large_frame, sizeof(large_frame), now_ms));
    until(APP_FLASH_JOURNAL_FULL);
    assert(programs == 3 && !app_flash_journal_begin(now_ms));
    const app_flash_journal_page_t end =
        decode(PAGE_COUNT - 1, APP_FLASH_JOURNAL_END);
    assert(end.end_reason == APP_FLASH_JOURNAL_FILLED &&
           end.written_frames == 1);
    reboot();
    until(APP_FLASH_JOURNAL_FULL);
    assert(app_flash_journal_status().next_page == PAGE_COUNT);
    assert(app_flash_journal_status().interrupted_sessions == 0);
}

static void test_fault(void) {
    clean_chip();
    reboot();
    until(APP_FLASH_JOURNAL_READY);
    assert(app_flash_journal_begin(now_ms));
    process();
    next_error = BSP_FLASH_VERIFY_FAILED;
    process();
    const app_flash_journal_status_t status = app_flash_journal_status();
    assert(status.state == APP_FLASH_JOURNAL_FAULT && status.next_page == 0);
    assert(status.flash_error == BSP_FLASH_VERIFY_FAILED);
    assert(!app_flash_journal_begin(now_ms));
    process();
    assert(programs == 0);
}

int main(void) {
    test_append_and_flush();
    test_recovery();
    test_full();
    test_fault();
    return 0;
}
