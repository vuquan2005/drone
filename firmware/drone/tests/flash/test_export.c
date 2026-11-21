#include "app_crc32.h"
#include "app_flash_journal.h"
#include "flash_export.h"
#include <assert.h>
#include <string.h>

enum { PAGE_COUNT = BSP_FLASH_SIZE_BYTES / BSP_FLASH_PAGE_BYTES };

static const char *control;
static bool rtt_available;
static bool fail_read;
static unsigned erases;
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
    (void)address;
    (void)out;
    (void)size;
    assert(false);
    return BSP_FLASH_SPI_ERROR;
}

bsp_flash_result_t bsp_flash_chip_erase_start(void) {
    assert(flash_op == NONE);
    ++erases;
    return BSP_FLASH_OK;
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
    if (erases == 0U && (read_index == 0U || read_index == PAGE_COUNT - 1U))
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
    unsigned budget = 2U * PAGE_COUNT + 32U;
    while (flash_export_get_state() != state) {
        assert(budget-- != 0U);
        flash_export_process(0U);
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
    assert(erases == 1U && reads - before_verify == PAGE_COUNT);
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
    return 0;
}
