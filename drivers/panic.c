/* panic.c — Kernel panic handler
 *
 * Displays a full diagnostic screen when the kernel hits an unrecoverable
 * error: CPU registers, exception info, a short stack trace, and the
 * last N kernel log messages (if the ring buffer is active).
 *
 * The screen uses a distinctive red-on-white colour scheme so it is
 * immediately obvious that something went wrong.
 *
 * This handler NEVER returns — it disables interrupts and halts.
 */

#include "../include/panic.h"
#include "../include/vga.h"
#include "../include/log.h"
#include "../include/process.h"
#include <stdint.h>
#include <stdarg.h>

/* ── Exception names ───────────────────────────────────────────────────── */
static const char *exception_names[] = {
    "Division Error",          "Debug",
    "Non-Maskable Interrupt",  "Breakpoint",
    "Overflow",                "Bound Range Exceeded",
    "Invalid Opcode",          "Device Not Available",
    "Double Fault",            "Coprocessor Segment Overrun",
    "Invalid TSS",             "Segment Not Present",
    "Stack Fault",             "General Protection Fault",
    "Page Fault",              "Reserved",
    "x87 FPU Error",           "Alignment Check",
    "Machine Check",           "SIMD Exception",
};
#define N_EXCEPTION_NAMES 20

/* ── Page fault error code bits ────────────────────────────────────────── */
#define PF_PRESENT  0x01
#define PF_WRITE    0x02
#define PF_USER     0x04
#define PF_RESERVED 0x08

/* ── Helpers ───────────────────────────────────────────────────────────── */

static void panic_border(void)
{
    vga_set_color(VGA_WHITE, VGA_RED);
    for (int i = 0; i < 80 * 25; i++)
        vga_putchar(' ');
    /* Reset cursor to top-left */
    extern void outb(uint16_t port, uint8_t value);
    outb(0x3D4, 14);
    outb(0x3D5, 0);
    outb(0x3D4, 15);
    outb(0x3D5, 0);
}

static void panic_puts_centered(const char *s, int row)
{
    int len = 0;
    const char *p = s;
    while (*p++) len++;
    int col = (80 - len) / 2;
    if (col < 0) col = 0;

    /* Set cursor position */
    extern void outb(uint16_t port, uint8_t value);
    uint16_t pos = (uint16_t)(row * 80 + col);
    outb(0x3D4, 14);
    outb(0x3D5, (uint8_t)(pos >> 8));
    outb(0x3D4, 15);
    outb(0x3D5, (uint8_t)(pos & 0xFF));

    vga_puts(s);
}

static void panic_hex(uint32_t v)
{
    const char *hex = "0123456789abcdef";
    char buf[11];
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 7; i >= 0; i--) {
        buf[2 + (7 - i)] = hex[(v >> (i * 4)) & 0xF];
    }
    buf[10] = '\0';
    vga_puts(buf);
}

static void panic_label(const char *label)
{
    vga_set_color(VGA_YELLOW, VGA_RED);
    vga_puts(label);
    vga_set_color(VGA_WHITE, VGA_RED);
}

/* ── Stack trace ───────────────────────────────────────────────────────── */

static void print_stack_trace(uint32_t ebp, unsigned int max_frames)
{
    panic_label("  Stack trace:\n");
    uint32_t *frame = (uint32_t *)ebp;

    for (unsigned int i = 0; i < max_frames; i++) {
        if ((uint32_t)frame < 0x00100000 || (uint32_t)frame >= 0xC0000000)
            break;
        if (frame[1] == 0)
            break;

        vga_puts("    #");
        /* Print frame number */
        if (i < 10)
            vga_putchar('0' + i);
        else
            vga_puts("??");

        vga_puts("  ");
        panic_hex(frame[1]);
        vga_puts("\n");

        frame = (uint32_t *)frame[0];
    }
}

/* ── Register dump ─────────────────────────────────────────────────────── */

static void print_regs(registers_t *regs)
{
    panic_label("  Registers:\n");

    vga_puts("    EAX="); panic_hex(regs->eax);
    vga_puts("  EBX="); panic_hex(regs->ebx);
    vga_puts("  ECX="); panic_hex(regs->ecx);
    vga_puts("\n");

    vga_puts("    EDX="); panic_hex(regs->edx);
    vga_puts("  ESI="); panic_hex(regs->esi);
    vga_puts("  EDI="); panic_hex(regs->edi);
    vga_puts("\n");

    vga_puts("    EBP="); panic_hex(regs->ebp);
    vga_puts("  EIP="); panic_hex(regs->eip);
    vga_puts("\n");

    vga_puts("    CS=");  panic_hex(regs->cs);
    vga_puts("   DS=");  panic_hex(regs->ds);
    vga_puts("   EFLAGS="); panic_hex(regs->eflags);
    vga_puts("\n");

    if (regs->cs != 0x08) {
        vga_puts("    ESP="); panic_hex(regs->user_esp);
        vga_puts("  SS=");  panic_hex(regs->user_ss);
        vga_puts("\n");
    }
}

/* ── Page fault details ────────────────────────────────────────────────── */

static void print_page_fault_info(uint32_t err_code)
{
    uint32_t cr2;
    __asm__ __volatile__("mov %%cr2, %0" : "=r"(cr2));

    panic_label("  Page fault details:\n");
    vga_puts("    Faulting address (CR2): ");
    panic_hex(cr2);
    vga_puts("\n");

    vga_puts("    Cause: ");
    if (!(err_code & PF_PRESENT))
        vga_puts("page not present");
    else if (err_code & PF_WRITE)
        vga_puts("write");
    else
        vga_puts("read");

    vga_puts(" from ");
    vga_puts((err_code & PF_USER) ? "user" : "kernel");
    vga_puts(" mode");

    if (err_code & PF_RESERVED)
        vga_puts(", reserved bit set");
    vga_puts("\n");
}

/* ── Process info ──────────────────────────────────────────────────────── */

static void print_process_info(void)
{
    if (current_proc) {
        panic_label("  Process:\n");
        vga_puts("    PID=");
        /* Simple int print */
        int pid = current_proc->pid;
        char buf[6];
        int i = 0;
        if (pid == 0) { vga_putchar('0'); }
        else {
            int tmp = pid;
            while (tmp > 0) { buf[i++] = '0' + (tmp % 10); tmp /= 10; }
            while (i--) vga_putchar(buf[i]);
        }
        vga_puts("  mode=");
        vga_puts(current_proc->mode ? "user" : "kernel");
        vga_puts("\n");
    }
}

/* ── Halting ───────────────────────────────────────────────────────────── */

static void halt_forever(void)
{
    __asm__ __volatile__("cli");
    for (;;) {
        __asm__ __volatile__("hlt");
    }
}

/* ── Public API ────────────────────────────────────────────────────────── */

/* Panic with a custom message (printf-like). */
__attribute__((noreturn)) void kernel_panic(const char *fmt, ...)
{
    panic_border();

    panic_puts_centered("========================================", 1);
    panic_puts_centered("KERNEL PANIC", 2);
    panic_puts_centered("========================================", 3);

    /* Print the user message */
    vga_set_color(VGA_WHITE, VGA_RED);
    int row = 5;
    int col = 4;

    /* Set cursor to row 5, col 4 */
    extern void outb(uint16_t port, uint8_t value);
    uint16_t pos = (uint16_t)(row * 80 + col);
    outb(0x3D4, 14);
    outb(0x3D5, (uint8_t)(pos >> 8));
    outb(0x3D4, 15);
    outb(0x3D5, (uint8_t)(pos & 0xFF));

    /* Inline format — just %s and %d and %x */
    va_list ap;
    va_start(ap, fmt);
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            if (*fmt == '\n') {
                row++;
                pos = (uint16_t)(row * 80 + col);
                outb(0x3D4, 14);
                outb(0x3D5, (uint8_t)(pos >> 8));
                outb(0x3D4, 15);
                outb(0x3D5, (uint8_t)(pos & 0xFF));
            } else {
                vga_putchar(*fmt);
            }
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
            if (n < 0) { vga_putchar('-'); u = (unsigned int)(-(n + 1)) + 1u; }
            else { u = (unsigned int)n; }
            char buf[12]; int i = 0;
            if (u == 0) { vga_putchar('0'); break; }
            while (u > 0) { buf[i++] = '0' + (u % 10); u /= 10; }
            while (i--) vga_putchar(buf[i]);
            break;
        }
        case 'x': {
            unsigned int n = va_arg(ap, unsigned int);
            panic_hex(n);
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

    /* Print stack trace from current EBP */
    uint32_t ebp;
    __asm__ __volatile__("mov %%ebp, %0" : "=r"(ebp));
    print_stack_trace(ebp, 16);

    /* Halting instruction */
    row += 18;
    pos = (uint16_t)(row * 80 + 4);
    outb(0x3D4, 14);
    outb(0x3D5, (uint8_t)(pos >> 8));
    outb(0x3D4, 15);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    vga_set_color(VGA_YELLOW, VGA_RED);
    vga_puts("System halted. Press reset to reboot.");

    halt_forever();
}

/* Panic from an unhandled CPU exception. */
__attribute__((noreturn)) void kernel_panic_exception(registers_t *regs)
{
    panic_border();

    panic_puts_centered("========================================", 0);
    panic_puts_centered("UNHANDLED CPU EXCEPTION", 1);
    panic_puts_centered("========================================", 2);

    vga_set_color(VGA_WHITE, VGA_RED);

    /* Position cursor at row 4 */
    extern void outb(uint16_t port, uint8_t value);
    uint16_t pos = (uint16_t)(4 * 80 + 2);
    outb(0x3D4, 14);
    outb(0x3D5, (uint8_t)(pos >> 8));
    outb(0x3D4, 15);
    outb(0x3D5, (uint8_t)(pos & 0xFF));

    /* Exception info */
    const char *name = (regs->int_no < N_EXCEPTION_NAMES)
                       ? exception_names[regs->int_no]
                       : "Unknown";

    panic_label("  Exception: ");
    vga_puts("#");
    {
        int n = (int)regs->int_no;
        char buf[4]; int i = 0;
        if (n == 0) vga_putchar('0');
        else { while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
               while (i--) vga_putchar(buf[i]); }
    }
    vga_puts(" (");
    vga_puts(name);
    vga_puts(")\n");

    vga_puts("  Error code: ");
    panic_hex(regs->err_code);
    vga_puts("\n\n");

    /* Page fault specific info */
    if (regs->int_no == 14)
        print_page_fault_info(regs->err_code);

    /* Register dump */
    print_regs(regs);
    vga_puts("\n");

    /* Process info */
    print_process_info();
    vga_puts("\n");

    /* Stack trace */
    print_stack_trace(regs->ebp, 12);

    /* Bottom bar */
    pos = (uint16_t)(23 * 80 + 2);
    outb(0x3D4, 14);
    outb(0x3D5, (uint8_t)(pos >> 8));
    outb(0x3D4, 15);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    vga_set_color(VGA_YELLOW, VGA_RED);
    vga_puts("System halted. Check EIP in stack trace to locate the fault.");

    halt_forever();
}
