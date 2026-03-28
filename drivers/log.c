/* log.c — Simple kernel logging facility implementation */

#include "../include/log.h"
#include "../include/vga.h"
#include <stdint.h>
#include <stdarg.h>

/* Global log level - defaults to INFO */
log_level_t current_log_level = LOG_LEVEL_INFO;

void log_init(void) {
    LOG_INFO("Logging system initialized");
}

void log_set_level(log_level_t level) {
    current_log_level = level;
    LOG_INFO("Log level set to %d", level);
}

void log_printf(log_level_t level, const char *fmt, ...) {
    if (level > current_log_level) {
        return;
    }

    /* Pick a colour prefix based on level */
    vga_color_t fg = VGA_LIGHT_GREY;
    switch (level) {
        case LOG_LEVEL_ERROR: fg = VGA_LIGHT_RED;    break;
        case LOG_LEVEL_WARN:  fg = VGA_YELLOW;       break;
        case LOG_LEVEL_INFO:  fg = VGA_LIGHT_CYAN;   break;
        case LOG_LEVEL_DEBUG: fg = VGA_LIGHT_GREEN;  break;
        default: break;
    }
    vga_set_color(fg, VGA_BLACK);

    /* Forward the format string and args to vga_printf */
    va_list ap;
    va_start(ap, fmt);
    /* vga_printf is already variadic — we can't call it directly with a
     * va_list, so we do a minimal inline formatter here. */
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            vga_putchar(*fmt);
            continue;
        }
        fmt++;
        switch (*fmt) {
            case 's': {
                const char *s = va_arg(ap, const char *);
                vga_puts(s ? s : "(null)");
                break;
            }
            case 'd': {
                int n = va_arg(ap, int);
                unsigned int u;
                if (n < 0) {
                    vga_putchar('-');
                    u = (unsigned int)(-(n + 1)) + 1u;
                } else {
                    u = (unsigned int)n;
                }
                char buf[12]; int i = 0;
                if (u == 0) { vga_putchar('0'); break; }
                while (u > 0) { buf[i++] = (char)('0' + (u % 10)); u /= 10; }
                while (i--) { vga_putchar(buf[i]); }
                break;
            }
            case 'u': {
                unsigned int u = va_arg(ap, unsigned int);
                char buf[12]; int i = 0;
                if (u == 0) { vga_putchar('0'); break; }
                while (u > 0) { buf[i++] = (char)('0' + (u % 10)); u /= 10; }
                while (i--) { vga_putchar(buf[i]); }
                break;
            }
            case 'x': {
                unsigned int n = va_arg(ap, unsigned int);
                char buf[10]; int i = 0;
                if (n == 0) { vga_puts("0x0"); break; }
                vga_puts("0x");
                while (n > 0) {
                    int d = n & 0xF;
                    buf[i++] = (d < 10) ? '0' + d : 'a' + d - 10;
                    n >>= 4;
                }
                while (i--) { vga_putchar(buf[i]); }
                break;
            }
            case '%':
                vga_putchar('%');
                break;
            default:
                vga_putchar('%');
                vga_putchar(*fmt);
                break;
        }
    }
    va_end(ap);

    /* Reset to default colour */
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}
