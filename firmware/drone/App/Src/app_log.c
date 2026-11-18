#include "app_log.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static app_log_clock_t clock_ms;

void app_log_init(app_log_clock_t clock) {
    clock_ms = clock;
}

static bool valid_tag(const char *tag) {
    if (tag == NULL || tag[0] == '\0') {
        return false;
    }
    unsigned length = 0;
    while (tag[length] != '\0') {
        char c = tag[length];
        if (length >= 16U ||
            !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-' ||
              c == '/')) {
            return false;
        }
        length++;
    }
    return true;
}

bsp_rtt_result_t app_log_write(unsigned level, const char *tag,
                               const char *format, ...) {
    static const char names[] = "-EWID";
    if (level == APP_LOG_LEVEL_OFF || level > APP_LOG_LEVEL_DEBUG ||
        !valid_tag(tag) || format == NULL) {
        return BSP_RTT_INVALID_ARGUMENT;
    }
    if (level > DRONE_LOG_LEVEL) {
        return BSP_RTT_OK;
    }
    if (clock_ms == NULL) {
        return BSP_RTT_NOT_INITIALIZED;
    }
    char line[256];
    int prefix = snprintf(line, sizeof(line), "[%lu][%c][%s] ",
                          (unsigned long)clock_ms(), names[level], tag);
    if (prefix < 0 || (size_t)prefix >= sizeof(line) - 2U) {
        return BSP_RTT_INVALID_ARGUMENT;
    }
    va_list args;
    va_start(args, format);
    /* Keep a byte for the final newline in addition to vsnprintf's NUL. */
    size_t available = sizeof(line) - (size_t)prefix - 1U;
    int message = vsnprintf(line + prefix, available, format, args);
    va_end(args);
    if (message < 0) {
        return BSP_RTT_INVALID_ARGUMENT;
    }
    size_t length;
    if ((size_t)message >= available) {
        static const char suffix[] = "...[truncated]";
        length = sizeof(line) - 2U;
        memcpy(line + length - (sizeof(suffix) - 1U), suffix,
               sizeof(suffix) - 1U);
    } else {
        length = (size_t)prefix + (size_t)message;
    }
    for (size_t i = (size_t)prefix; i < length; ++i) {
        if (line[i] == '\n' || line[i] == '\r') {
            line[i] = ' ';
        }
    }
    line[length++] = '\n';
    line[length] = '\0';
    return bsp_rtt_write(BSP_RTT_LOG, line, length);
}
