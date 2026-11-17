#ifndef RUNTIME_H
#define RUNTIME_H

#include "app_command.h"

typedef enum {
    RUNTIME_BOOT_SELECT,
    RUNTIME_FLIGHT,
    RUNTIME_ESC_CALIB,
    RUNTIME_FLASH_EXPORT,
} runtime_mode_t;

/** Initialize shared RC input and disable motor outputs before selection.
 * Requires initialized HAL, clocks, GPIO, UART/DMA and TIM4. Call once after
 * reset, from foreground; calling while a mode is active is unsupported.
 * Initializes shared RTT, logging and telemetry. Does not initialize the flight
 * App, IMU or start PWM.
 */
void runtime_init(void);

/** Poll RC once and dispatch the selected mode from serialized foreground.
 * Requires runtime_init. BOOT_SELECT waits for valid RC, arm OFF and throttle
 * <= APP_ARM_THROTTLE_MAX. CH7/SB HIGH selects ESC_CALIB with priority;
 * otherwise telemetry STREAM selects FLASH_EXPORT, OFF/RECORD select FLIGHT.
 * CH6/SC is a flight-mode request and never selects a maintenance operation.
 * CH8/SD retains its telemetry request meaning after FLIGHT is selected.
 * Selection latches until reset. ESC_CALIB initializes its own PWM session;
 * FLASH_EXPORT remains a placeholder without PWM or flash I/O. No flight App
 * initialization occurs in either maintenance mode. RC telemetry runs during
 * boot selection. FLIGHT init may block on IMU SPI; flight processing starts on
 * the next poll with a fresh receiver snapshot.
 */
void runtime_process(void);

/** Return latched mode (BOOT_SELECT before selection). No I/O, foreground only.
 */
runtime_mode_t runtime_get_mode(void);

/** Whether the selected mode has an implementation (FLIGHT and ESC_CALIB).
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
