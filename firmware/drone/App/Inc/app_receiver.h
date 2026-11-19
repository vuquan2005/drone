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

/** Poll transport and refresh a caller-owned cached snapshot only on change.
 * Before first call after receiver init, zero-initialize both sample and
 * sequence; preserve them between calls. Each valid RC frame advances the
 * sequence modulo 2^32 even when timestamps/channels are identical. Connection
 * changes (timeout or transport discontinuity) also refresh the snapshot.
 * Returns true only when snapshot was refreshed; false leaves both unchanged.
 * Do not modify the cache externally; each consumer owns its sample/sequence.
 * NULL arguments return false without polling. Poll frequently (target <=2 ms)
 * even without updates: timeout/error detection must not depend on new frames.
 * All access is serialized foreground only. Pointers are distinct, writable
 * for the call and not retained. Do not skip 2^32 frames between calls.
 */
bool app_receiver_update_sample(app_receiver_sample_t *sample,
                                uint32_t *sequence);

/** Try to publish a printable ASCII Flight Mode label (1 to 13 characters).
 * Returns true if transmission started. A busy UART or invalid label returns
 * false without queuing data. The label is read during the call only.
 * Nonblocking, serialized foreground only, after app_receiver_init(). */
bool app_receiver_send_flight_mode(const char *label);

/** Try to publish a CRSF Attitude frame for a fresh body FRD-to-NED unit
 * quaternion (w,x,y,z). The quaternion is read during the call only.
 * Returns true if transmission started. Invalid input or a busy UART
 * returns false without queuing data. Nonblocking, serialized foreground
 * only, after app_receiver_init(). */
bool app_receiver_send_attitude(const float body_to_ned[4]);

#endif
