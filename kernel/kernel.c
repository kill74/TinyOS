/* kernel.c — Kernel entry point
 *
 * kernel_main() is called by boot.S after the stack is set up.
 * It initialises every subsystem in dependency order and then
 * enters an idle loop, waiting for interrupts to fire.
 *
 * Boot sequence:
 *   GDT → Paging → IDT → IRQs → Timer → Keyboard → kmalloc → sti → idle
 */

#include "../include/gdt.h"
#include "../include/idt.h"
#include "../include/irq.h"
#include "../include/paging.h"
#include "../include/timer.h"
#include "../include/keyboard.h"
#include "../include/kmalloc.h"
#include "../include/vga.h"
#include "../include/log.h"
#include "../include/process.h"
#include "../include/syscall.h"
#include <stdint.h>

/* The kernel's bump allocator will own memory from 2 MB to 4 MB.
 * (The first 2 MB are used by the kernel image itself.) */
#define HEAP_START 0x00200000 /* 2 MB */
#define HEAP_END 0x00400000   /* 4 MB */

/* ── Decorative helpers ───────────────────────────────────────────────────── */

static void print_separator(void)
{
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_puts("  ────────────────────────────────────────\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

static void print_ok(const char *label)
{
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_puts("  [ ");
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("OK");
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_puts(" ]  ");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_puts(label);
    vga_putchar('\n');
}

/* ── Kernel entry ─────────────────────────────────────────────────────────── */

/* Forward declarations for test processes, the developer shell, and a userprog runner */
void process1_task(void);
void process2_task(void);
void shell_task(void);
void userprog_run(void);

/* Simple string length function */
static size_t strlen(const char *str)
{
    size_t len = 0;
    while (str[len] != '\0')
    {
        len++;
    }
    return len;
}

void kernel_main(void)
{
    /* ── 1. VGA terminal — must be first so we can print anything ────────── */
    vga_init();

    /* Initialize logging system */
    log_init();

    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_puts("\n      Tiny OS\n");
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_puts("      Minimal x86 Kernel\n\n");
    print_separator();

    /* ── 2. GDT — defines code/data segments; must precede IDT ──────────── */
    init_gdt();
    print_ok("GDT  initialised (5 descriptors: null, kcode, kdata, ucode, udata)");

    /* ── 3. Paging — enable virtual memory before we do much else ─────────  */
    init_paging();
    print_ok("MMU  enabled (first 4 MB identity-mapped)");

    /* ── 4. IDT — install exception/interrupt gates ───────────────────────  */
    init_idt();
    print_ok("IDT  loaded (32 exception gates + 16 IRQ gates)");

    /* ── 5. IRQ — remap the 8259 PIC so hardware IRQs don't clash ─────────  */
    init_irq();
    print_ok("PIC  remapped (IRQ0–15  →  vectors 32–47)");

    /* ── 6. Timer — PIT channel 0 at 100 Hz ──────────────────────────────  */
    init_timer(100);
    print_ok("PIT  running at 100 Hz (IRQ0 enabled)");

    /* ── 7. Keyboard — PS/2 port, scancode set 1 ─────────────────────────  */
    init_keyboard();
    print_ok("KB   PS/2 keyboard ready (IRQ1 enabled)");

    /* ── 8. Heap — simple bump allocator ─────────────────────────────────  */
    kmalloc_init(HEAP_START, HEAP_END);
    print_ok("HEAP bump allocator: 2 MB pool starting at 0x00200000");

    /* ── 9. Process Management ───────────────────────────────────────────  */
    init_processing();
    print_ok("Process manager initialized");

    /* Register syscall handler */
    isr_register_handler(0x80, syscall_handler);
    LOG_INFO("Syscall handler registered");

    /* Create test processes */
    proc_create(process1_task, 1);
    proc_create(process2_task, 2);
    /* Start interactive shell for developers */
    proc_create(shell_task, 3);
    /* Start tiny user-program runner (A) */
    proc_create(userprog_run, 4);
    LOG_INFO("Created test processes");

    /* ── Demo: allocate, use, then FREE — no leaks ───────────────────────  */
    typedef struct
    {
        uint32_t x;
        uint32_t y;
    } point_t;

    point_t *p = (point_t *)kmalloc(sizeof(point_t));
    point_t *p2 = (point_t *)kmalloc(sizeof(point_t));
    p->x = 10;
    p->y = 20;
    p2->x = 100;
    p2->y = 200;

    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_printf("        alloc: p=(%u,%u)  p2=(%u,%u)\n",
               p->x, p->y, p2->x, p2->y);

    /* Free both allocations and verify the heap coalesces cleanly */
    kfree(p);
    kfree(p2);
    kmalloc_check(); /* walk every block and verify canaries */
    kmalloc_stats(); /* print used/free summary */
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);

    print_separator();

    /* ── 10. Enable hardware interrupts ───────────────────────────────────  */
    extern void interrupts_enable(void);
    interrupts_enable();

    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_puts("\n  All subsystems online. Interrupts enabled.\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_puts("  Type on your keyboard — characters will echo here.\n");
    vga_puts("  Timer ticks are running silently in the background.\n\n");

    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_puts("  > ");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);

    /* ── Idle loop ────────────────────────────────────────────────────────  */
    /* `hlt` puts the CPU to sleep until the next interrupt fires.
     * This is far better than a busy-wait: it saves power and lets the
     * timer/keyboard handlers run cleanly. */
    while (1)
    {
        __asm__ __volatile__("hlt");
    }
}

/* Simple test processes */
void process1_task(void)
{
    const char *msg = "Process 1 running...\n";
    while (1)
    {
        sys_write(1, msg, strlen(msg));
        sys_sleep(25); /* 250 ms at 100 Hz */
    }
}

void process2_task(void)
{
    const char *msg = "Process 2 running...\n";
    while (1)
    {
        sys_write(1, msg, strlen(msg));
        sys_sleep(40); /* 400 ms at 100 Hz */
    }
}
