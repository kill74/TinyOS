/* vga.c — VGA text-mode terminal driver
 *
 * The VGA hardware exposes an 80×25 character grid starting at physical address
 * 0xB8000. Each cell is 2 bytes:
 *   byte 0 — ASCII character code
 *   byte 1 — colour attribute: bits[7:4] = background, bits[3:0] = foreground
 *
 * We also talk to the VGA controller over I/O ports to move the hardware cursor.
 */

#include "../include/vga.h"

/* ── Constants ────────────────────────────────────────────────────────────── */
#define VGA_WIDTH   80
#define VGA_HEIGHT  25
#define VGA_BUFFER  ((volatile uint16_t *)0xB8000)

/* VGA controller I/O ports */
#define VGA_CTRL_REG   0x3D4
#define VGA_DATA_REG   0x3D5
#define VGA_CURSOR_HI  14
#define VGA_CURSOR_LO  15

/* ── I/O port helpers (declared in helpers.S) ────────────────────────────── */
extern void    outb(uint16_t port, uint8_t value);
extern uint8_t inb(uint16_t port);

/* ── Module state ─────────────────────────────────────────────────────────── */
static int    term_row;
static int    term_col;
static uint8_t term_color;           /* packed colour attribute byte */

/* ── Internal helpers ─────────────────────────────────────────────────────── */

/* Pack foreground + background into a single attribute byte. */
static inline uint8_t make_color(vga_color_t fg, vga_color_t bg) {
    return (uint8_t)((bg << 4) | (fg & 0x0F));
}

/* Pack character + colour into a 16-bit VGA cell.
 * Cast to uint8_t first to avoid sign-extension: a char value of e.g. 0x80
 * would become 0xFF80 as uint16_t (corrupting the colour byte) without it. */
static inline uint16_t make_entry(char c, uint8_t color) {
    return (uint16_t)(uint8_t)c | ((uint16_t)color << 8);
}

/* Move the blinking hardware cursor to match our software position. */
static void update_cursor(void) {
    uint16_t pos = (uint16_t)(term_row * VGA_WIDTH + term_col);
    outb(VGA_CTRL_REG, VGA_CURSOR_HI);
    outb(VGA_DATA_REG, (uint8_t)(pos >> 8));
    outb(VGA_CTRL_REG, VGA_CURSOR_LO);
    outb(VGA_DATA_REG, (uint8_t)(pos & 0xFF));
}

/* Scroll the screen up by one row when we reach the bottom. */
static void scroll(void) {
    /* Use the current colour for the new blank line, not a hardcoded default. */
    uint16_t empty = make_entry(' ', term_color);

    /* Shift every row up by one */
    for (int row = 1; row < VGA_HEIGHT; row++) {
        for (int col = 0; col < VGA_WIDTH; col++) {
            VGA_BUFFER[(row - 1) * VGA_WIDTH + col] =
                VGA_BUFFER[row * VGA_WIDTH + col];
        }
    }

    /* Clear the last row */
    for (int col = 0; col < VGA_WIDTH; col++) {
        VGA_BUFFER[(VGA_HEIGHT - 1) * VGA_WIDTH + col] = empty;
    }

    term_row = VGA_HEIGHT - 1;
}

/* ── Public API ───────────────────────────────────────────────────────────── */

void vga_init(void) {
    term_row   = 0;
    term_col   = 0;
    term_color = make_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_clear();
}

void vga_set_color(vga_color_t fg, vga_color_t bg) {
    term_color = make_color(fg, bg);
}

void vga_clear(void) {
    uint16_t blank = make_entry(' ', term_color);
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        VGA_BUFFER[i] = blank;
    }
    term_row = 0;
    term_col = 0;
    update_cursor();
}

void vga_putchar(char c) {
    if (c == '\n') {
        term_col = 0;
        term_row++;
    } else if (c == '\r') {
        term_col = 0;
    } else if (c == '\t') {
        /* Advance to next 4-column tab stop */
        term_col = (term_col + 4) & ~3;
    } else if (c == '\b') {
        if (term_col > 0) {
            /* Normal case: erase the character to the left */
            term_col--;
        } else if (term_row > 0) {
            /* At the start of a row: retreat to the last column of the row above */
            term_row--;
            term_col = VGA_WIDTH - 1;
        }
        /* Erase the cell now under the cursor in both cases */
        if (term_col > 0 || term_row >= 0) {
            VGA_BUFFER[term_row * VGA_WIDTH + term_col] =
                make_entry(' ', term_color);
        }
    } else {
        VGA_BUFFER[term_row * VGA_WIDTH + term_col] =
            make_entry(c, term_color);
        term_col++;
    }

    /* Wrap long lines */
    if (term_col >= VGA_WIDTH) {
        term_col = 0;
        term_row++;
    }

    /* Scroll if past the last row */
    if (term_row >= VGA_HEIGHT) {
        scroll();
    }

    update_cursor();
}

void vga_puts(const char *s) {
    while (*s) {
        vga_putchar(*s++);
    }
}

/* Minimal printf: supports %s, %c, %d, %u, %x, %% */
void vga_printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            vga_putchar(*fmt);
            continue;
        }

        fmt++;  /* skip '%' */
        switch (*fmt) {
            case 's': {
                const char *s = va_arg(args, const char *);
                vga_puts(s ? s : "(null)");
                break;
            }
            case 'c': {
                vga_putchar((char)va_arg(args, int));
                break;
            }
            case 'd': {
                int n = va_arg(args, int);
                /* Guard against INT_MIN: -INT_MIN overflows signed int (UB).
                 * Widen to unsigned before negating so the arithmetic is safe. */
                unsigned int u;
                if (n < 0) {
                    vga_putchar('-');
                    u = (unsigned int)(-(n + 1)) + 1u;  /* safe negate */
                } else {
                    u = (unsigned int)n;
                }
                char buf[12]; int i = 0;
                if (u == 0) { vga_putchar('0'); break; }
                while (u > 0) { buf[i++] = (char)('0' + (u % 10)); u /= 10; }
                while (i--) vga_putchar(buf[i]);
                break;
            }
            case 'u': {
                unsigned int u = va_arg(args, unsigned int);
                char buf[12]; int i = 0;
                if (u == 0) { vga_putchar('0'); break; }
                while (u > 0) { buf[i++] = (char)('0' + (u % 10)); u /= 10; }
                while (i--) vga_putchar(buf[i]);
                break;
            }
            case 'x': {
                unsigned int n = va_arg(args, unsigned int);
                char buf[10]; int i = 0;
                if (n == 0) { vga_puts("0x0"); break; }
                vga_puts("0x");
                while (n > 0) {
                    int d = n & 0xF;
                    buf[i++] = (d < 10) ? '0' + d : 'a' + d - 10;
                    n >>= 4;
                }
                while (i--) vga_putchar(buf[i]);
                break;
            }
            case '%': {
                vga_putchar('%');
                break;
            }
            default:
                vga_putchar('%');
                vga_putchar(*fmt);
                break;
        }
    }

    va_end(args);
}
