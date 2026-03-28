/* log.c — Simple kernel logging facility implementation */

/* Use include path relative to this file to avoid path resolution issues */
#include "../include/log.h"
#include <stdint.h>
#include <stdarg.h>

/* Minimal internal helpers to format integers for log_printf */
static int int_to_dec(char *out, int value) {
    char tmp[12]; int n = 0; int v = value; int neg = 0;
    if (v == 0) { out[0] = '0'; return 1; }
    if (v < 0) { neg = 1; v = -v; }
    while (v) { tmp[n++] = '0' + (v % 10); v /= 10; }
    int pos = 0; if (neg) out[pos++] = '-';
    for (int i = n-1; i >= 0; i--) out[pos++] = tmp[i];
    return pos;
}
static int uint_to_dec(char *out, unsigned int value) {
    char tmp[12]; int n = 0; if (value == 0) { out[0] = '0'; return 1; }
    while (value) { tmp[n++] = '0' + (value % 10); value /= 10; }
    int pos = 0; for (int i = n-1; i >= 0; i--) out[pos++] = tmp[i];
    return pos;
}
static int uint_to_hex(char *out, unsigned int value) {
    char tmp[9]; int n = 0; if (value == 0) { out[0] = '0'; return 1; }
    const char *hexd = "0123456789abcdef";
    while (value) { tmp[n++] = hexd[value & 0xF]; value >>= 4; }
    int pos = 0; for (int i = n-1; i >= 0; i--) out[pos++] = tmp[i];
    return pos;
}


/* Global log level - defaults to INFO */
log_level_t current_log_level = LOG_LEVEL_INFO;

void log_init(void) {
    /* Log system initialized at INFO level by default */
    LOG_INFO("Logging system initialized");
}

void log_set_level(log_level_t level) {
    current_log_level = level;
    LOG_INFO("Log level set to %d", level);
}

/* Optional: provide a no-op implementation for log_printf to keep ABI stable
 * in environments where the full formatting engine isn't wired yet. */
void log_printf(log_level_t level, const char *fmt, ...) {
    (void)level; (void)fmt; va_list ap; va_start(ap, fmt); va_end(ap);
}
