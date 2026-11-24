#ifndef APP_FLASH_JOURNAL_H
#define APP_FLASH_JOURNAL_H

#include "bsp_flash.h"
#include <stdbool.h>

#define APP_FLASH_JOURNAL_HEADER_BYTES 24U
#define APP_FLASH_JOURNAL_PAYLOAD_BYTES                                        \
    (BSP_FLASH_PAGE_BYTES - APP_FLASH_JOURNAL_HEADER_BYTES)

typedef enum {
    APP_FLASH_JOURNAL_BEGIN = 1,
    APP_FLASH_JOURNAL_DATA,
    APP_FLASH_JOURNAL_END,
} app_flash_journal_page_type_t;

typedef enum {
    APP_FLASH_JOURNAL_STOPPED = 1,
    APP_FLASH_JOURNAL_FILLED,
} app_flash_journal_end_reason_t;

typedef struct {
    app_flash_journal_page_type_t type;
    uint16_t payload_size;
    uint32_t session_id;
    uint32_t uptime_ms;
    app_flash_journal_end_reason_t end_reason;
    uint32_t written_frames;
    uint32_t dropped_frames;
} app_flash_journal_page_t;

typedef enum {
    APP_FLASH_JOURNAL_SCANNING,
    APP_FLASH_JOURNAL_READY,
    APP_FLASH_JOURNAL_OPENING,
    APP_FLASH_JOURNAL_OPEN,
    APP_FLASH_JOURNAL_WRITING,
    APP_FLASH_JOURNAL_CLOSING,
    APP_FLASH_JOURNAL_FULL,
    APP_FLASH_JOURNAL_FAULT,
} app_flash_journal_state_t;

typedef struct {
    app_flash_journal_state_t state;
    uint32_t scanned_pages;
    uint32_t next_page;
    uint32_t ignored_pages;
    uint32_t interrupted_sessions;
    bsp_flash_result_t flash_error;
} app_flash_journal_status_t;

/*
 * One foreground owner exclusively uses the BSP flash while the journal runs.
 * Call these APIs outside ISRs. Timestamps are MCU uptime in milliseconds
 * modulo 2^32 and use the same clock throughout.
 */

/** @brief Start recovery after BSP hardware initialization, once per boot.
 * @param now_ms Current uptime. The probe starts after the chip startup delay.
 */
void app_flash_journal_init(uint32_t now_ms);

/** @brief Run one bounded step of probing, scanning or writing.
 * @param now_ms Current uptime. A partial data page is flushed after 100 ms. */
void app_flash_journal_process(uint32_t now_ms);

/** @brief Return the current state and cumulative scan diagnostics.
 * @return Ignored pages include malformed and orphan non-erased pages.
 * Interrupted counts sessions without a matching valid END. Flash error is
 * meaningful in FAULT. */
app_flash_journal_status_t app_flash_journal_status(void);

/** @brief Schedule a BEGIN page if READY, with at least two DATA pages free.
 * @param now_ms Session start uptime.
 * @return True if accepted. Wait for OPEN before submitting frames. */
bool app_flash_journal_begin(uint32_t now_ms);

/** @brief Copy one encoded DRT! frame into the current data page.
 * @param frame Non-NULL complete encoder output including its DRT! CRC.
 * @param size Frame size, at most APP_FLASH_JOURNAL_PAYLOAD_BYTES.
 * @param now_ms Uptime when the frame was submitted.
 * @return True if copied. False leaves the frame with the caller. If the
 * current page cannot fit it, a flush is scheduled and the caller may retry
 * when OPEN. The caller accounts for frames it chooses to discard. */
bool app_flash_journal_append(const void *frame, size_t size, uint32_t now_ms);

/** @brief Account for discarded frames in this session.
 * @param count Number of dropped frames. The counter saturates at UINT32_MAX.
 * @note Also accepts frames stranded by FULL until the END transfer starts. */
void app_flash_journal_note_drops(uint32_t count);

/** @brief Stop accepting frames and schedule flushing and END.
 * @param now_ms Uptime when close was requested.
 * @return True if a session accepted the request. Wait for READY, FULL or
 * FAULT to observe completion. */
bool app_flash_journal_close(uint32_t now_ms);

/** @brief Validate a page envelope and decode its metadata.
 * @param page Non-NULL buffer of BSP_FLASH_PAGE_BYTES bytes.
 * @param index Physical page index expected in its header.
 * @param out Non-NULL output, modified only on success. DATA begins at
 * APP_FLASH_JOURNAL_HEADER_BYTES and retains the original frame CRCs.
 * @return True for a supported page with a valid CRC and record layout. */
bool app_flash_journal_decode(const uint8_t *page, uint32_t index,
                              app_flash_journal_page_t *out);

/** @brief Check the CRC and bounds of a checkpoint in the final sector.
 * @param page Complete flash page, or NULL.
 * @param index Physical page index of the supplied bytes.
 * @return True if the checkpoint envelope is valid. The END is checked
 * separately.
 */
bool app_flash_journal_is_marker(const uint8_t *page, uint32_t index);

/** @brief Encode a checkpoint for a chip just verified fully erased.
 * @param page Writable buffer of BSP_FLASH_PAGE_BYTES bytes, non-NULL.
 * @return Byte address where the caller must program and verify the page.
 */
uint32_t app_flash_journal_encode_empty_marker(uint8_t *page);

#endif
