#ifndef RUNTIME_H
#define RUNTIME_H

#include "app_command.h"

typedef enum {
    RUNTIME_BOOT_SELECT,
    RUNTIME_FLIGHT,
    RUNTIME_ESC_CALIB,
    RUNTIME_FLASH_EXPORT,
    RUNTIME_IMU_CALIB,
} runtime_mode_t;

/** Initialize shared RC input and disable motor outputs before selection.
 * Requires initialized HAL, clocks, GPIO, UART/DMA and TIM4. Call once after
 * reset, from foreground; calling while a mode is active is unsupported.
 * Initializes shared RTT, logging and telemetry. Does not initialize the flight
 * App, IMU or start PWM.
 */
void runtime_init(void);

/** Poll RC once and dispatch the selected mode from serialized foreground.
 * Requires runtime_init. BOOT_SELECT waits for valid RC. CH7/SB HIGH with
 * CH6/SC MIDDLE selects IMU_CALIB. Other CH7 HIGH requests select ESC_CALIB.
 * With CH7 below HIGH, telemetry STREAM selects
 * FLASH_EXPORT and OFF/RECORD select FLIGHT. Arm and throttle are ignored
 * during mode selection.
 * After boot selection, CH6/SC remains the flight-mode request in FLIGHT.
 * CH8/SD retains its telemetry request meaning after FLIGHT is selected.
 * Selection latches until reset. ESC_CALIB initializes its own PWM session;
 * IMU_CALIB keeps PWM off and waits for a new RC frame after initializing its
 * sensor. FLASH_EXPORT remains a placeholder. Flight App initialization only
 * occurs in FLIGHT. RTT receiver telemetry
 * runs during boot selection. CRSF status is offered to UART every 500 ms
 * across runtime modes, including boot selection. FLIGHT init may block on
 * IMU SPI; flight processing starts on the next poll with a fresh RC snapshot.
 */
void runtime_process(void);

/** Return latched mode (BOOT_SELECT before selection). No I/O, foreground only.
 */
runtime_mode_t runtime_get_mode(void);

/** Whether the selected mode has an implementation.
 * False while waiting for selection. No I/O, serialized foreground only.
 * This is not flight health or motor permission; use app_get_status in FLIGHT.
 */
bool runtime_mode_supported(void);

/** Copy the latest decoded RC command; false only for NULL output.
 * Requires runtime_init. Check valid; no polling or age check is performed.
 * Nonblocking, serialized foreground only; output is not retained.
 */
bool runtime_get_command(app_command_t *command);

#endif
