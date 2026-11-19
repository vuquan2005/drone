#include "app_receiver.h"
#include "bsp_receiver.h"
#include "main.h"
#include <stddef.h>

static crsf_t receiver;
static uint32_t frame_sequence;

void app_receiver_init(void) {
    crsf_init(&receiver);
    frame_sequence = 0U;
    (void)bsp_receiver_init();
}

void app_receiver_poll(void) {
    uint8_t byte;
    uint32_t timestamp_ms;
    // Bound foreground work even when bytes arrive continuously.
    for (uint16_t i = 0; i < 256U; i++) {
        bsp_receiver_result_t result = bsp_receiver_read(&byte, &timestamp_ms);
        if (result == BSP_RECEIVER_EMPTY) {
            break;
        }
        if (result == BSP_RECEIVER_DISCONTINUITY) {
            crsf_reset_stream(&receiver);
            receiver.valid = false;
        } else {
            if (crsf_feed(&receiver, byte, timestamp_ms)) {
                frame_sequence++;
            }
        }
    }
}

bool app_receiver_get_sample(app_receiver_sample_t *sample) {
    if (!sample) {
        return false;
    }
    // Drain pending transport errors before reporting the last RC sample.
    app_receiver_poll();
    for (uint8_t i = 0; i < CRSF_CHANNEL_COUNT; i++) {
        sample->channels[i] = receiver.channels[i];
    }
    sample->last_frame_ms = receiver.last_frame_ms;
    sample->connected =
        receiver.valid && (uint32_t)(HAL_GetTick() - receiver.last_frame_ms) <
                              APP_RECEIVER_TIMEOUT_MS;
    return true;
}

bool app_receiver_update_sample(app_receiver_sample_t *sample,
                                uint32_t *sequence) {
    if (sample == NULL || sequence == NULL) {
        return false;
    }
    app_receiver_poll();
    const bool connected =
        receiver.valid && (uint32_t)(HAL_GetTick() - receiver.last_frame_ms) <
                              APP_RECEIVER_TIMEOUT_MS;
    if (*sequence == frame_sequence && sample->connected == connected) {
        return false;
    }
    for (unsigned i = 0; i < CRSF_CHANNEL_COUNT; ++i) {
        sample->channels[i] = receiver.channels[i];
    }
    sample->last_frame_ms = receiver.last_frame_ms;
    sample->connected = connected;
    *sequence = frame_sequence;
    return true;
}

bool app_receiver_send_flight_mode(const char *label) {
    uint8_t frame[CRSF_FLIGHT_MODE_FRAME_MAX];
    const uint8_t length = crsf_encode_flight_mode(label, frame, sizeof(frame));
    return length != 0U && bsp_receiver_try_send(frame, length);
}

bool app_receiver_send_attitude(const float body_to_ned[4]) {
    uint8_t frame[CRSF_ATTITUDE_FRAME_SIZE];
    const uint8_t length =
        crsf_encode_attitude(body_to_ned, frame, sizeof(frame));
    return length != 0U && bsp_receiver_try_send(frame, length);
}
