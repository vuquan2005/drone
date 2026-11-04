#ifndef APP_RECEIVER_H
#define APP_RECEIVER_H

#include "crsf.h"
#include <stdbool.h>
#include <stdint.h>

#define APP_RECEIVER_TIMEOUT_MS 100U

typedef struct {
    uint16_t
        channels[CRSF_CHANNEL_COUNT]; /**< Raw CRSF values, channels 1–16. */
    uint32_t last_frame_ms; /**< HAL tick at receipt; wraps modulo 2^32. */
    bool connected; /**< A valid RC frame was received within the timeout. */
} app_receiver_sample_t;

/** Start USART1 reception. Call once after UART initialization, outside ISR. */
void app_receiver_init(void);
/** @brief Drain buffered UART bytes and update RC state. Foreground only;
 * call frequently (target <= 2 ms; 255 queued bytes maximum). */
void app_receiver_poll(void);
/** Copy a coherent snapshot after polling the receiver. Returns false for NULL.
 * All receiver API calls must be serialized in foreground; connected is false
 * at startup and after APP_RECEIVER_TIMEOUT_MS without RC. Old channel values
 * are retained while disconnected. connected describes RC frame freshness, not
 * radio LQ or motor failsafe. */
bool app_receiver_get_sample(app_receiver_sample_t *sample);

#endif
