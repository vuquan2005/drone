#ifndef APP_COMMAND_H
#define APP_COMMAND_H

#include "app_receiver.h"
#include "normalized.h"

#define APP_COMMAND_RC_MIN INT32_C(172)
#define APP_COMMAND_RC_CENTER INT32_C(992)
#define APP_COMMAND_RC_MAX INT32_C(1811)
#define APP_COMMAND_DEADBAND INT32_C(10)

/** Raw switch position; flight-mode behavior is assigned by the flight App. */
typedef enum {
    APP_COMMAND_SWITCH_LOW,
    APP_COMMAND_SWITCH_MIDDLE,
    APP_COMMAND_SWITCH_HIGH,
} app_command_switch_t;

/** Requested telemetry destination; the command decoder performs no I/O. */
typedef enum {
    APP_COMMAND_TELEMETRY_OFF,
    APP_COMMAND_TELEMETRY_STREAM,
    APP_COMMAND_TELEMETRY_RECORD,
} app_command_telemetry_mode_t;

/** Jumper T20 model04 mapping, dimensionless Q15 (ONE=32768).
 * Positive axes follow increasing radio channel values; verify radio direction
 * against body FRD before flight. No gyro FLU conversion belongs here.
 * valid is RC validity only, not permission to run motors. Invalid commands
 * have every field zeroed: mode LOW, telemetry OFF and requests false. */
typedef struct {
    norm_q15_t roll, pitch,
        yaw;             /**< [-ONE,ONE], deadband rescaled to endpoints. */
    norm_q15_t throttle; /**< [0,ONE], no deadband. Not calibrated thrust. */
    bool arm_requested;  /**< CH5 high only; not an armed state. */
    app_command_switch_t mode;      /**< CH6/SC flight-mode request;
                                       position-to-controller mapping pending. */
    bool esc_calibration_requested; /**< CH7 HIGH level, not an edge or
                                       permission. */
    app_command_telemetry_mode_t
        telemetry_mode;     /**< CH8 LOW/MIDDLE/HIGH = OFF/RECORD/STREAM. */
    uint32_t last_frame_ms; /**< Receiver HAL tick; wraps modulo 2^32. */
    bool valid;
} app_command_t;

/** Map a receiver snapshot without hardware access, clocks or retained state.
 * CH1/2/3/4 = roll/pitch/throttle/yaw, CH5/6/7/8 = SA/SC/SB/SD.
 * Assumes nominal endpoints 172/992/1811; clips extended travel within the
 * CRSF 11-bit range. Values >2047 in any used channel invalidate the command.
 * Switch LOW/HIGH start at the midpoints between MIN/CENTER/MAX; arm is true
 * only in HIGH. CH7 HIGH requests ESC calibration; CH8 LOW/MIDDLE/HIGH
 * requests telemetry OFF/RECORD/STREAM. No calibration or telemetry action is
 * performed. No switch hysteresis, arming latch, expo or rate limit.
 * Caller must supply a freshly obtained receiver snapshot each foreground
 * iteration; connected must already describe freshness. NULL/disconnected
 * input clears output and returns false. NULL output returns false.
 * Pointers must be distinct and valid for the call. Nonblocking, no ISR state.
 */
bool app_command_decode(const app_receiver_sample_t *sample,
                        app_command_t *command);

#endif
