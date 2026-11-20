#include "app_flash_recorder.h"
#include "app_flash_journal.h"
#include "app_telemetry.h"
#include <string.h>

enum { FIFO_BYTES = 2048, ITEM_HEADER_BYTES = 1 + sizeof(uint32_t) };

_Static_assert(APP_FLASH_JOURNAL_PAYLOAD_BYTES <= UINT8_MAX,
               "FIFO frame length must fit in one byte");

static struct {
    uint8_t bytes[FIFO_BYTES];
    size_t head;
    size_t tail;
    size_t used;
    uint32_t queued;
    uint32_t sample_ms;
    uint32_t stop_ms;
    bool requested;
    bool session;
    bool stopping;
} recorder;

static size_t advance(size_t offset, size_t size) {
    return (offset + size) % FIFO_BYTES;
}

static void fifo_write(const void *data, size_t size) {
    const size_t remaining = FIFO_BYTES - recorder.head;
    const size_t first = size < remaining ? size : remaining;
    memcpy(recorder.bytes + recorder.head, data, first);
    memcpy(recorder.bytes, (const uint8_t *)data + first, size - first);
    recorder.head = advance(recorder.head, size);
    recorder.used += size;
}

static void fifo_read(size_t offset, void *data, size_t size) {
    const size_t remaining = FIFO_BYTES - offset;
    const size_t first = size < remaining ? size : remaining;
    memcpy(data, recorder.bytes + offset, first);
    memcpy((uint8_t *)data + first, recorder.bytes, size - first);
}

void app_flash_recorder_init(uint32_t now_ms) {
    memset(&recorder, 0, sizeof(recorder));
    app_flash_journal_init(now_ms);
}

void app_flash_recorder_update(bool valid, bool requested, uint32_t now_ms) {
    recorder.sample_ms = now_ms;
    if (!valid) {
        return;
    }
    recorder.requested = requested;
    if (!requested && recorder.session && !recorder.stopping) {
        recorder.stopping = true;
        recorder.stop_ms = now_ms;
    }
}

bool app_flash_recorder_accepting(void) {
    const app_flash_journal_state_t state = app_flash_journal_status().state;
    return recorder.session && !recorder.stopping &&
           (state == APP_FLASH_JOURNAL_OPEN ||
            state == APP_FLASH_JOURNAL_WRITING);
}

bool app_flash_recorder_prepare(size_t size) {
    if (size == 0U || size > APP_FLASH_JOURNAL_PAYLOAD_BYTES)
        return false;
    if (!app_flash_recorder_accepting()) {
        if (recorder.session &&
            app_flash_journal_status().state == APP_FLASH_JOURNAL_CLOSING)
            app_flash_journal_note_drops(1U);
        return false;
    }
    if (size + ITEM_HEADER_BYTES > FIFO_BYTES - recorder.used) {
        app_flash_journal_note_drops(1U);
        return false;
    }
    return true;
}

void app_flash_recorder_enqueue(const uint8_t *frame, size_t size) {
    const uint8_t length = (uint8_t)size;
    fifo_write(&length, sizeof(length));
    fifo_write(&recorder.sample_ms, sizeof(recorder.sample_ms));
    fifo_write(frame, size);
    ++recorder.queued;
}

static void submit_head(void) {
    uint32_t timestamp_ms;
    uint8_t frame[APP_FLASH_JOURNAL_PAYLOAD_BYTES];
    const size_t length = recorder.bytes[recorder.tail];
    fifo_read(advance(recorder.tail, 1U), &timestamp_ms, sizeof(timestamp_ms));
    fifo_read(advance(recorder.tail, ITEM_HEADER_BYTES), frame, length);
    if (app_flash_journal_append(frame, length, timestamp_ms)) {
        const size_t size = ITEM_HEADER_BYTES + length;
        recorder.tail = advance(recorder.tail, size);
        recorder.used -= size;
        --recorder.queued;
    }
}

static void clear_session(void) {
    recorder.tail = recorder.head;
    recorder.used = 0;
    recorder.queued = 0;
    recorder.session = false;
    recorder.stopping = false;
}

void app_flash_recorder_process(uint32_t now_ms,
                                app_config_t (*get_config)(void)) {
    const app_flash_journal_state_t state = app_flash_journal_status().state;
    switch (state) {
    case APP_FLASH_JOURNAL_READY:
        if (recorder.requested && app_flash_journal_begin(now_ms)) {
            recorder.session = true;
            const app_config_t config = get_config();
            uint8_t frame[APP_TELEMETRY_CONFIG_BYTES];
            /* Flash stores the same CONFIG layout, without an RTT request. */
            app_telemetry_encode_config(frame, 0U, &config);
            app_flash_recorder_enqueue(frame, sizeof(frame));
        }
        break;
    case APP_FLASH_JOURNAL_OPEN:
        if (recorder.queued != 0U) {
            submit_head();
        }
        if (recorder.stopping && recorder.queued == 0U) {
            (void)app_flash_journal_close(recorder.stop_ms);
        }
        /* An old queued timestamp must not cause one write per frame during
         * a backlog. Let queued frames fill the current page first. */
        if (recorder.queued != 0U &&
            app_flash_journal_status().state == APP_FLASH_JOURNAL_OPEN) {
            return;
        }
        break;
    case APP_FLASH_JOURNAL_OPENING:
    case APP_FLASH_JOURNAL_WRITING:
        if (recorder.stopping && recorder.queued == 0U) {
            (void)app_flash_journal_close(recorder.stop_ms);
        }
        break;
    case APP_FLASH_JOURNAL_CLOSING:
        app_telemetry_flush_recording_imu();
        /* The last data page may leave queued frames without flash space. */
        app_flash_journal_note_drops(recorder.queued);
        clear_session();
        break;
    case APP_FLASH_JOURNAL_FULL:
    case APP_FLASH_JOURNAL_FAULT:
        clear_session();
        break;
    default:
        break;
    }
    app_flash_journal_process(now_ms);
}
