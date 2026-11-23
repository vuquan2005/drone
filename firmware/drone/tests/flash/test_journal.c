#include "app_flash_journal.h"
#include "app_log.h"
#include "app_telemetry.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

enum { PAGE_COUNT = BSP_FLASH_SIZE_BYTES / BSP_FLASH_PAGE_BYTES };

static uint8_t storage[PAGE_COUNT][BSP_FLASH_PAGE_BYTES];
static uint8_t pending_page[BSP_FLASH_PAGE_BYTES];
static enum { IDLE, PROBING, READING, PROGRAMMING } operation;
static uint32_t *probe_out;
static uint32_t jedec_id;
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
        *probe_out = jedec_id;
        if (jedec_id != BSP_FLASH_JEDEC_ID) {
            operation = IDLE;
            return BSP_FLASH_WRONG_DEVICE;
        }
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
    jedec_id = BSP_FLASH_JEDEC_ID;
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
    app_flash_journal_note_drops(1U);
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
    storage[PAGE_COUNT - 5][0] = 0;
    reboot();
    until(APP_FLASH_JOURNAL_READY);
    assert(app_flash_journal_begin(now_ms));
    until(APP_FLASH_JOURNAL_OPEN);
    assert(app_flash_journal_append(large_frame, sizeof(large_frame), now_ms));
    until(APP_FLASH_JOURNAL_OPEN);
    assert(app_flash_journal_append(large_frame, sizeof(large_frame), now_ms));
    until(APP_FLASH_JOURNAL_FULL);
    assert(programs == 4 && !app_flash_journal_begin(now_ms));
    const app_flash_journal_page_t end =
        decode(PAGE_COUNT - 1, APP_FLASH_JOURNAL_END);
    assert(end.end_reason == APP_FLASH_JOURNAL_FILLED &&
           end.written_frames == 2);
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

#if DRONE_RTT
static bool rtt_full;
static uint8_t rtt_imu[141];
static unsigned rtt_writes;

bsp_rtt_result_t bsp_rtt_prepare_write(bsp_rtt_channel_t channel, size_t size) {
    assert(channel == BSP_RTT_DATA && size <= sizeof(rtt_imu));
    return rtt_full ? BSP_RTT_FULL : BSP_RTT_OK;
}

bsp_rtt_result_t bsp_rtt_write(bsp_rtt_channel_t channel, const void *data,
                               size_t size) {
    assert(channel == BSP_RTT_DATA && size <= sizeof(rtt_imu));
    assert(!rtt_full);
    memcpy(rtt_imu, data, size);
    ++rtt_writes;
    return BSP_RTT_OK;
}

uint32_t bsp_rtt_get_dropped_full(bsp_rtt_channel_t channel) {
    (void)channel;
    return 0;
}

size_t bsp_rtt_read_control(void *data, size_t size) {
    (void)data;
    (void)size;
    return 0;
}
#endif

uint32_t bsp_time_now_us(void) {
    return now_ms * 1000U;
}

static app_config_t flight_config(void) {
    return (app_config_t){.mode = APP_CONFIG_FLIGHT, .mahony_hz = 200U};
}

static void recorder_until(app_flash_journal_state_t target) {
    uint32_t budget = 2 * PAGE_COUNT + 200;
    while (state() != target) {
        assert(budget-- != 0U);
        app_flash_recorder_process(now_ms, flight_config);
    }
}

static void recorder_start(void) {
    app_telemetry_init();
    app_flash_recorder_init(now_ms);
    app_flash_recorder_update(true, true, now_ms);
    now_ms += 6;
    recorder_until(APP_FLASH_JOURNAL_OPEN);
}

static void queue_samples(uint32_t first, uint32_t count) {
    for (uint32_t sequence = first; sequence < first + count; ++sequence) {
        const app_imu_sample_t sample = {
            .t_read_us = sequence * 1250U,
            .sensor_time_ticks = sequence * 32U,
            .accel_raw = {(int16_t)sequence},
        };
        app_telemetry_queue_imu(&sample, sequence);
    }
}

static const uint8_t *data_payload(uint32_t index, uint16_t size) {
    assert(decode(index, APP_FLASH_JOURNAL_DATA).payload_size == size);
    return storage[index] + APP_FLASH_JOURNAL_HEADER_BYTES;
}

static void test_recorder_fifo_and_sinks(void) {
    clean_chip();
    recorder_start();
#if DRONE_RTT
    rtt_full = false;
#endif
    queue_samples(1, 8);
#if DRONE_RTT
    assert(rtt_writes == 1U);
    uint8_t first_frame[141];
    memcpy(first_frame, rtt_imu, sizeof(first_frame));
    rtt_full = true;
#endif

    app_flash_recorder_update(false, false, 90U);
    queue_samples(9, 96); /* 12 more batches, one over the FIFO budget. */
#if DRONE_RTT
    rtt_full = false;
#endif
    queue_samples(105, 8); /* Flash FIFO full, RTT still receives the batch. */
#if DRONE_RTT
    assert(rtt_writes == 2U && rtt_imu[8] == 105U);
#endif
    app_flash_recorder_update(true, false, 141U);
    now_ms = 142U;
    app_flash_recorder_update(true, true, now_ms);
    assert(!app_flash_recorder_prepare(141U));
    recorder_until(APP_FLASH_JOURNAL_READY);

    const uint8_t *config = data_payload(1, 220);
    assert(config[5] == 10U && config[8] == 0U &&
           config[13] == APP_CONFIG_FLIGHT);
    const uint8_t *first = data_payload(2, 141);
    assert(first[5] == 9U && first[8] == 1U && first[19] == 8U);
#if DRONE_RTT
    assert(memcmp(first, first_frame, sizeof(first_frame)) == 0);
#endif
    for (uint32_t i = 1; i < 12; ++i) {
        const uint8_t *batch = data_payload(i + 2, 141);
        assert(batch[5] == 9U && batch[8] == 1U + i * 8U && batch[19] == 8U);
    }
    const app_flash_journal_page_t end = decode(14, APP_FLASH_JOURNAL_END);
    assert(end.written_frames == 13 && end.dropped_frames == 2);
    assert(end.uptime_ms == 141 && end.end_reason == APP_FLASH_JOURNAL_STOPPED);

    /* A partial RTT-only batch predates the next session. */
    queue_samples(113, 2);
    recorder_until(APP_FLASH_JOURNAL_OPEN);
    assert(decode(15, APP_FLASH_JOURNAL_BEGIN).session_id == 15);
    queue_samples(115, 3);
    app_telemetry_flush_recording_imu();
    now_ms = 150U;
    app_flash_recorder_update(true, false, now_ms);
    queue_samples(118, 1);
    recorder_until(APP_FLASH_JOURNAL_READY);
    assert(data_payload(16, 220)[5] == 10U);
    const uint8_t *last = data_payload(17, 66);
    assert(last[5] == 9U && last[8] == 115U && last[19] == 3U);
    assert(decode(18, APP_FLASH_JOURNAL_END).written_frames == 2);
}

static void test_recorder_full(void) {
    clean_chip();
    storage[PAGE_COUNT - 5][0] = 0;
    recorder_start();
    queue_samples(1, 18); /* Two batches plus one partial. */
    recorder_until(APP_FLASH_JOURNAL_FULL);
    assert(data_payload(PAGE_COUNT - 3, 220)[5] == 10U);
    assert(data_payload(PAGE_COUNT - 2, 141)[8] == 1U);
    const app_flash_journal_page_t end =
        decode(PAGE_COUNT - 1, APP_FLASH_JOURNAL_END);
    assert(end.written_frames == 2 && end.dropped_frames == 2);
    assert(end.end_reason == APP_FLASH_JOURNAL_FILLED);
}

#if APP_LOG_ENABLED(APP_LOG_LEVEL_INFO)
static char last_flash_log[192];
static unsigned flash_log_level;
static unsigned flash_log_count;

bsp_rtt_result_t app_log_write(unsigned level, const char *tag,
                               const char *format, ...) {
    assert(strcmp(tag, "flash") == 0 || strcmp(tag, "telemetry") == 0);
    va_list args;
    va_start(args, format);
    const int length =
        vsnprintf(last_flash_log, sizeof(last_flash_log), format, args);
    va_end(args);
    assert(length > 0 && (size_t)length < sizeof(last_flash_log));
    flash_log_level = level;
    ++flash_log_count;
    return BSP_RTT_OK;
}

static void test_record_request_logs(void) {
    clean_chip();
    flash_log_count = 0;
    app_flash_recorder_init(now_ms);
    assert(flash_log_count == 1U);
    app_flash_recorder_update(true, true, 1U);
    assert(flash_log_count == 2U);
    assert(strcmp(last_flash_log,
                  "record_request enabled=1 state=scanning scanned_pages=0") ==
           0);

    app_flash_recorder_update(true, true, 2U);
    app_flash_recorder_update(false, false, 3U);
    assert(flash_log_count == 2U);

    app_flash_recorder_update(true, false, 4U);
    assert(flash_log_count == 3U);
    assert(strcmp(last_flash_log,
                  "record_request enabled=0 state=scanning scanned_pages=0") ==
           0);
}

static void test_flash_logs(void) {
    clean_chip();
    flash_log_count = 0;
    storage[0][0] = 0; /* A non-journal page counts as ignored. */
    reboot();
    assert(strcmp(last_flash_log, "scan_start pages=65536") == 0);
    until(APP_FLASH_JOURNAL_READY);
    assert(flash_log_count == 2U);
    assert(strcmp(last_flash_log,
                  "scan_done jedec=0xef4018 elapsed_ms=6 next_page=1 "
                  "ignored_pages=1 interrupted_sessions=0") == 0);

    assert(app_flash_journal_begin(now_ms));
    process(); /* Write started but is not verified. */
    assert(flash_log_count == 2U);
    until(APP_FLASH_JOURNAL_OPEN);
    assert(flash_log_level == APP_LOG_LEVEL_INFO);
    assert(strcmp(last_flash_log, "session_open id=1 addr=0x000100") == 0);
    assert(app_flash_journal_append(large_frame, sizeof(large_frame), now_ms));
    until(APP_FLASH_JOURNAL_OPEN);
    assert(flash_log_count == 3U); /* Regular DATA pages are quiet. */
    app_flash_journal_note_drops(2U);
    assert(app_flash_journal_close(now_ms));
    assert(strcmp(last_flash_log, "session_closing id=1 reason=stopped") == 0);
    process();
    assert(flash_log_count == 4U);
    until(APP_FLASH_JOURNAL_READY);
    assert(flash_log_count == 5U);
    assert(strcmp(last_flash_log, "session_close id=1 reason=stopped "
                                  "written_frames=1 dropped_frames=2") == 0);

    reboot();
    process(); /* Start the probe. */
    process(); /* Complete the probe. */
    process(); /* Start reading page zero. */
    next_error = BSP_FLASH_TIMEOUT;
    process();
    assert(flash_log_level == APP_LOG_LEVEL_ERROR);
    assert(strstr(last_flash_log, "op=read error=timeout") != NULL);
    assert(strstr(last_flash_log, "page=0 addr=0x000000") != NULL);
    const unsigned after_fault = flash_log_count;
    process();
    assert(flash_log_count == after_fault);

    jedec_id = 0x123456U;
    reboot();
    until(APP_FLASH_JOURNAL_FAULT);
    assert(strstr(last_flash_log, "op=probe error=wrong_device") != NULL);
    assert(strstr(last_flash_log, "jedec=0x123456 expected=0xef4018") != NULL);
}
#endif

int main(void) {
    test_append_and_flush();
    test_recovery();
    test_full();
    test_fault();
    test_recorder_fifo_and_sinks();
    test_recorder_full();
#if APP_LOG_ENABLED(APP_LOG_LEVEL_INFO)
    test_flash_logs();
    test_record_request_logs();
#endif
    return 0;
}
