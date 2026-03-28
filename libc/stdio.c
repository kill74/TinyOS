/* stdio.c — Standard I/O with formatted output */

#include "../libc/stdio.h"
#include "../libc/string.h"
#include <stdint.h>
#include <stdarg.h>

/* ── Low-level output (calls VGA directly for now) ───────────────────────── */
/* In a full OS these would go through syscalls. For kernel-mode apps,
 * we call VGA directly. */

extern void vga_putchar(char c);
extern void vga_puts(const char *s);

int write(int fd, const void *buf, size_t count)
{
    (void)fd;
    const char *p = (const char *)buf;
    for (size_t i = 0; i < count; i++)
        vga_putchar(p[i]);
    return (int)count;
}

int read(int fd, void *buf, size_t count)
{
    (void)fd;
    (void)buf;
    (void)count;
    /* Not implemented yet — would need keyboard syscalls */
    return 0;
}

/* ── Formatted output engine ─────────────────────────────────────────────── */

typedef struct {
    char  *buf;     /* NULL = output to VGA, else write to buffer */
    size_t size;    /* buffer size (ignored if buf==NULL) */
    size_t pos;     /* current write position */
} fmt_out_t;

static void fmt_putc(fmt_out_t *out, char c)
{
    if (out->buf) {
        if (out->pos < out->size - 1)
            out->buf[out->pos] = c;
    } else {
        vga_putchar(c);
    }
    out->pos++;
}

static void fmt_puts(fmt_out_t *out, const char *s)
{
    while (*s) fmt_putc(out, *s++);
}

static void fmt_int(fmt_out_t *out, long val, int base, int width, int pad, int upper)
{
    char tmp[24];
    int i = 0;
    int neg = 0;
    unsigned long u;

    if (val < 0 && base == 10) {
        neg = 1;
        u = (unsigned long)(-(val + 1)) + 1ul;
    } else {
        u = (unsigned long)val;
    }

    if (u == 0) { tmp[i++] = '0'; }
    else {
        while (u > 0) {
            int d = u % base;
            tmp[i++] = (d < 10) ? '0' + d :
                       (upper ? 'A' + d - 10 : 'a' + d - 10);
            u /= base;
        }
    }

    int total = i + (neg ? 1 : 0);
    while (total < width) { fmt_putc(out, pad); total++; }
    if (neg) fmt_putc(out, '-');
    while (i > 0) fmt_putc(out, tmp[--i]);
}

static void fmt_uint(fmt_out_t *out, unsigned long val, int base, int width, int pad, int upper)
{
    char tmp[24];
    int i = 0;

    if (val == 0) { tmp[i++] = '0'; }
    else {
        while (val > 0) {
            int d = val % base;
            tmp[i++] = (d < 10) ? '0' + d :
                       (upper ? 'A' + d - 10 : 'a' + d - 10);
            val /= base;
        }
    }

    while (i < width) { fmt_putc(out, pad); }
    while (i > 0) fmt_putc(out, tmp[--i]);
}

static int fmt_vprintf(fmt_out_t *out, const char *fmt, va_list ap)
{
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            fmt_putc(out, *fmt);
            continue;
        }
        fmt++;

        /* Parse flags */
        char pad_char = ' ';
        if (*fmt == '0') { pad_char = '0'; fmt++; }

        /* Parse width */
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        /* Parse length modifier */
        int is_long = 0;
        if (*fmt == 'l') { is_long = 1; fmt++; }

        /* Format specifier */
        switch (*fmt) {
        case 'd': case 'i': {
            long v = is_long ? va_arg(ap, long) : (long)va_arg(ap, int);
            fmt_int(out, v, 10, width, pad_char, 0);
            break;
        }
        case 'u': {
            unsigned long v = is_long ? va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned int);
            fmt_uint(out, v, 10, width, pad_char, 0);
            break;
        }
        case 'x':
        case 'X': {
            unsigned long v = is_long ? va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned int);
            fmt_uint(out, v, 16, width, pad_char, *fmt == 'X');
            break;
        }
        case 'o': {
            unsigned long v = is_long ? va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned int);
            fmt_uint(out, v, 8, width, pad_char, 0);
            break;
        }
        case 'p': {
            fmt_puts(out, "0x");
            fmt_uint(out, (unsigned long)va_arg(ap, void *), 16, 8, '0', 0);
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            int slen = (int)strlen(s);
            while (slen < width) { fmt_putc(out, ' '); slen++; }
            fmt_puts(out, s);
            break;
        }
        case 'c':
            fmt_putc(out, (char)va_arg(ap, int));
            break;
        case '%':
            fmt_putc(out, '%');
            break;
        default:
            fmt_putc(out, '%');
            fmt_putc(out, *fmt);
            break;
        }
    }

    return (int)out->pos;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

int printf(const char *fmt, ...)
{
    fmt_out_t out = { .buf = NULL, .size = 0, .pos = 0 };
    va_list ap;
    va_start(ap, fmt);
    int ret = fmt_vprintf(&out, fmt, ap);
    va_end(ap);
    return ret;
}

int sprintf(char *buf, const char *fmt, ...)
{
    fmt_out_t out = { .buf = buf, .size = 0x7FFFFFFF, .pos = 0 };
    va_list ap;
    va_start(ap, fmt);
    int ret = fmt_vprintf(&out, fmt, ap);
    va_end(ap);
    if (out.buf && out.pos < 0x7FFFFFFF)
        buf[out.pos] = '\0';
    return ret;
}

int snprintf(char *buf, size_t size, const char *fmt, ...)
{
    fmt_out_t out = { .buf = buf, .size = size, .pos = 0 };
    va_list ap;
    va_start(ap, fmt);
    int ret = fmt_vprintf(&out, fmt, ap);
    va_end(ap);
    if (buf && size > 0) {
        size_t end = out.pos < size ? out.pos : size - 1;
        buf[end] = '\0';
    }
    return ret;
}

int puts(const char *s)
{
    vga_puts(s);
    vga_putchar('\n');
    return (int)strlen(s) + 1;
}

int putchar(int c)
{
    vga_putchar((char)c);
    return c;
}

void perror(const char *s)
{
    if (s && *s) {
        vga_puts(s);
        vga_puts(": ");
    }
    vga_puts("error\n");
}
