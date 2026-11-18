#ifndef BSP_FLASH_H
#define BSP_FLASH_H

#include <stddef.h>
#include <stdint.h>

#define BSP_FLASH_SIZE_BYTES (16U * 1024U * 1024U)
#define BSP_FLASH_PAGE_BYTES 256U
#define BSP_FLASH_JEDEC_ID 0xEF4018U

typedef enum {
    BSP_FLASH_OK,
    BSP_FLASH_PENDING,
    BSP_FLASH_IDLE,
    BSP_FLASH_OPERATION_ACTIVE,
    BSP_FLASH_INVALID_ARGUMENT,
    BSP_FLASH_NOT_PROBED,
    BSP_FLASH_WRONG_DEVICE,
    BSP_FLASH_DEVICE_BUSY,
    BSP_FLASH_WRITE_PROTECTED,
    BSP_FLASH_NOT_ERASED,
    BSP_FLASH_VERIFY_FAILED,
    BSP_FLASH_SPI_ERROR,
    BSP_FLASH_TIMEOUT,
} bsp_flash_result_t;

/*
 * Use after CubeMX initialization, from one foreground caller. Start functions
 * return PENDING on acceptance and OPERATION_ACTIVE if a previous operation
 * still needs to be polled.
 * Poll the accepted operation to completion before starting another.
 */

/** @brief Start a JEDEC probe. The caller must retain id until poll completes.
 * @param id Receives the 24-bit ID even on WRONG_DEVICE, or stays unchanged
 * if the transfer fails. Must be non-NULL.
 * @return PENDING, OPERATION_ACTIVE, INVALID_ARGUMENT or a latched transport
 * failure. */
bsp_flash_result_t bsp_flash_probe_start(uint32_t *id);

/** @brief Start reading Status Register 1, including BUSY and WEL.
 * @param status Non-NULL output, valid on OK from poll. Retain until then.
 * @return PENDING, OPERATION_ACTIVE, INVALID_ARGUMENT, NOT_PROBED or a latched
 * transport failure.
 */
bsp_flash_result_t bsp_flash_status_start(uint8_t *status);

/** @brief Start a read of 1 to 256 bytes within flash.
 * @param address Byte offset. Reads may cross page boundaries.
 * @param out Non-NULL caller buffer, valid on OK from poll. Retain until then.
 * @param size Number of bytes to read.
 * @return PENDING, OPERATION_ACTIVE, INVALID_ARGUMENT, NOT_PROBED or a latched
 * transport failure.
 */
bsp_flash_result_t bsp_flash_read_start(uint32_t address, void *out,
                                        size_t size);

/** @brief Verify the target is erased, program it and read it back.
 * @param address Byte offset. The entire range must be within one page.
 * @param data Non-NULL source, copied before return.
 * @param size Number of bytes to program, from 1 to 256.
 * @return PENDING, OPERATION_ACTIVE, INVALID_ARGUMENT, NOT_PROBED or a latched
 * transport failure.
 */
bsp_flash_result_t bsp_flash_program_start(uint32_t address, const void *data,
                                           size_t size);

/** @brief Advance one transaction step without waiting for DMA or flash BUSY.
 * @return PENDING while active, IDLE when no operation is pending, or a
 * terminal result. SPI errors, timeouts and failed readback latch until reset.
 */
bsp_flash_result_t bsp_flash_poll(void);

#endif
