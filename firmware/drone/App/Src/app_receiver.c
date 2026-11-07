#include "app_receiver.h"
#include "bsp_receiver.h"
#include "main.h"

static crsf_t receiver;

void app_receiver_init(void) {
    crsf_init(&receiver);
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
            (void)crsf_feed(&receiver, byte, timestamp_ms);
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
