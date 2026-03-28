#include "../include/syscall.h"
#include "../include/stdarg.h"

void u_putchar(char c) {
    sys_write(1, &c, 1);
}
void u_puts(const char *s) {
    while (*s) u_putchar(*s++);
}
void u_putint(int n) {
    if (n == 0) { u_putchar('0'); return; }
    char buf[12]; int i = 0;
    if (n < 0) { u_putchar('-'); n = -n; }
    while (n) { buf[i++] = '0' + (n % 10); n /= 10; }
    while (i--) u_putchar(buf[i]);
}
void u_printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    while (*fmt) {
        if (*fmt == '%' && fmt[1] == 'd') {
            int val = va_arg(ap, int);
            u_putint(val);
            fmt += 2;
        } else if (*fmt == '%' && fmt[1] == 's') {
            const char *s = va_arg(ap, const char*);
            u_puts(s);
            fmt += 2;
        } else {
            u_putchar(*fmt++);
        }
    }
    va_end(ap);
}