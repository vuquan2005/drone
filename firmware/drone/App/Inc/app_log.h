/** @file app_log.h
 * @brief Bounded text logs on RTT channel 0, one foreground writer.
 */
#ifndef APP_LOG_H
#define APP_LOG_H

#include "bsp_rtt.h"

#define APP_LOG_LEVEL_OFF 0
#define APP_LOG_LEVEL_ERROR 1
#define APP_LOG_LEVEL_WARN 2
#define APP_LOG_LEVEL_INFO 3
#define APP_LOG_LEVEL_DEBUG 4

#ifndef DRONE_LOG_LEVEL
#define DRONE_LOG_LEVEL APP_LOG_LEVEL_OFF
#endif

typedef uint32_t (*app_log_clock_t)(void);

#if DRONE_RTT && DRONE_LOG_LEVEL > APP_LOG_LEVEL_OFF
/** @brief Set the millisecond uptime provider after initializing RTT.
 * @param clock_ms Non-NULL foreground clock callback, e.g. HAL_GetTick.
 * NULL disables logging until initialized with a clock. Does not reset RTT.
 * @note Foreground-only, serialize init and all log writes. Timestamp is taken
 * when formatting the log, not when an earlier event/sample occurred.
 */
void app_log_init(app_log_clock_t clock_ms);

/** @brief Format one line [uptime_ms][E/W/I/D][tag] message and enqueue it.
 * @param level ERROR through DEBUG, using APP_LOG_LEVEL_* constants.
 * @param tag 1..16 ASCII letters, digits, '.', '_', '-' or '/'; no spaces.
 * @param format Non-NULL printf format for text without embedded NUL bytes.
 * @return RTT result; filtered levels return OK without writing. Invalid
 * arguments/format failures return INVALID_ARGUMENT; no clock returns
 * NOT_INITIALIZED. Overlong messages end with ...[truncated] and newline.
 * @note Foreground-only, no allocation, maximum 255 bytes including newline.
 * CR/LF inside messages become spaces. Use APP_LOG_* macros so compile-time
 * filtering also removes argument evaluation. Never logs a write failure.
 */
bsp_rtt_result_t app_log_write(unsigned level, const char *tag,
                               const char *format, ...)
    __attribute__((format(printf, 3, 4)));
#else
static inline void app_log_init(app_log_clock_t clock_ms) {
    (void)clock_ms;
}
#endif

#if DRONE_RTT && DRONE_LOG_LEVEL >= APP_LOG_LEVEL_ERROR
#define APP_LOG_ERROR(tag, ...)                                                \
    ((void)app_log_write(APP_LOG_LEVEL_ERROR, tag, __VA_ARGS__))
#else
#define APP_LOG_ERROR(...) ((void)0)
#endif
#if DRONE_RTT && DRONE_LOG_LEVEL >= APP_LOG_LEVEL_WARN
#define APP_LOG_WARN(tag, ...)                                                 \
    ((void)app_log_write(APP_LOG_LEVEL_WARN, tag, __VA_ARGS__))
#else
#define APP_LOG_WARN(...) ((void)0)
#endif
#if DRONE_RTT && DRONE_LOG_LEVEL >= APP_LOG_LEVEL_INFO
#define APP_LOG_INFO(tag, ...)                                                 \
    ((void)app_log_write(APP_LOG_LEVEL_INFO, tag, __VA_ARGS__))
#else
#define APP_LOG_INFO(...) ((void)0)
#endif
#if DRONE_RTT && DRONE_LOG_LEVEL >= APP_LOG_LEVEL_DEBUG
#define APP_LOG_DEBUG(tag, ...)                                                \
    ((void)app_log_write(APP_LOG_LEVEL_DEBUG, tag, __VA_ARGS__))
#else
#define APP_LOG_DEBUG(...) ((void)0)
#endif

#endif
