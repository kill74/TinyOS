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
#include "../include/tss.h"
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
#include "../include/elf.h"
#include "../net/rtl8139.h"
#include "../net/ethernet.h"
#include "../net/arp.h"
#include "../net/ip.h"
#include "../net/tcp.h"
#include "../net/udp.h"
#include "../net/socket.h"
#include "../net/packet.h"
#include "../gui/graphics.h"
#include "../gui/mouse.h"
#include "../gui/gui.h"
#include "../fs/fs.h"
#include "../tests/tests.h"
#include <stdint.h>

/* The kernel's bump allocator will own memory from 2 MB to 4 MB.
 * (The first 2 MB are used by the kernel image itself.) */
#define HEAP_START 0x00200000 /* 2 MB */
#define HEAP_END 0x00400000   /* 4 MB */

/* ── Boot splash ────────────────────────────────────────────────────────── */

/* Total number of init steps (for progress bar width). */
#define BOOT_STEPS 12
static int boot_step;

static void boot_splash_begin(void)
{
    vga_init();

    /* Dark background fill */
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_clear();

    /* Logo — compact, modern look */
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_puts("\n");
    vga_puts("   +--------------------------+\n");
    vga_puts("   |        T I N Y O S       |\n");
    vga_puts("   +--------------------------+\n");
    vga_puts("\n");

    /* Progress bar container */
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_puts("   [");
    for (int i = 0; i < BOOT_STEPS * 3; i++) vga_putchar('.');
    vga_puts("]\n\n");
    vga_putchar('\n');

    boot_step = 0;
}

static void boot_splash_step(const char *label)
{
    /* Move cursor to fill the progress bar */
    int bar_col = 4 + boot_step * 3;

    /* Fill this segment of the bar */
    extern void outb(uint16_t port, uint8_t value);
    uint16_t pos = (uint16_t)(4 * 80 + bar_col);
    outb(0x3D4, 14);
    outb(0x3D5, (uint8_t)(pos >> 8));
    outb(0x3D4, 15);
    outb(0x3D5, (uint8_t)(pos & 0xFF));

    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("###");

    /* Print the label below the bar */
    int label_row = 6 + boot_step;
    pos = (uint16_t)(label_row * 80 + 4);
    outb(0x3D4, 14);
    outb(0x3D5, (uint8_t)(pos >> 8));
    outb(0x3D4, 15);
    outb(0x3D5, (uint8_t)(pos & 0xFF));

    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_puts("[");
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts("+");
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_puts("] ");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_puts(label);

    boot_step++;
}

static void boot_splash_done(void)
{
    /* Clear screen and show ready state */
    vga_clear();

    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_puts("\n   TinyOS is ready.\n\n");
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_puts("   Initializing desktop...\n\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

/* ── Kernel entry ─────────────────────────────────────────────────────────── */

/* Forward declarations for test processes, the developer shell, and a userprog runner */
void process1_task(void);
void process2_task(void);
void shell_task(void);
void userprog_run(void);

/* Network stack initialisation */
static void net_init(void)
{
    packet_init();
    if (rtl8139_init() == 0) {
        ethernet_init(*rtl8139_get_mac());
        arp_init();
        ip_set_config(
            ip_from_bytes(10, 0, 2, 15),   /* 10.0.2.15 */
            ip_from_bytes(255, 255, 255, 0),
            ip_from_bytes(10, 0, 2, 2)      /* 10.0.2.2  */
        );
        tcp_init();
        udp_init();
        socket_init();
        arp_insert(ip_from_bytes(10, 0, 2, 2),
                   (eth_addr_t){ { 0x52, 0x55, 0x0A, 0x00, 0x02, 0x02 } });
    }
}

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
    /* ── Boot splash ────────────────────────────────────────────────────── */
    boot_splash_begin();
    log_init();

    /* ── 1. GDT + TSS ───────────────────────────────────────────────────── */
    init_gdt();
    extern uint32_t stack_top;
    init_tss((uint32_t)&stack_top);
    boot_splash_step("GDT + TSS (segments, task state)");

    /* ── 2. Paging ──────────────────────────────────────────────────────── */
    init_paging();
    boot_splash_step("Paging (virtual memory, 4 MB identity map)");

    /* ── 3. IDT ─────────────────────────────────────────────────────────── */
    init_idt();
    boot_splash_step("IDT (32 exceptions + 16 IRQs + syscall)");

    /* ── 4. IRQ / PIC ───────────────────────────────────────────────────── */
    init_irq();
    boot_splash_step("PIC (8259 remap, IRQ 0-15)");

    /* ── 5. Timer ───────────────────────────────────────────────────────── */
    init_timer(100);
    boot_splash_step("Timer (PIT @ 100 Hz, preemptive scheduler)");

    /* ── 6. Keyboard ────────────────────────────────────────────────────── */
    init_keyboard();
    boot_splash_step("Keyboard (PS/2, scancode set 1)");

    /* ── 7. Heap ────────────────────────────────────────────────────────── */
    kmalloc_init(HEAP_START, HEAP_END);
    boot_splash_step("Heap (2 MB pool, free-list + canaries)");

    /* ── 8. Process manager ─────────────────────────────────────────────── */
    init_processing();
    boot_splash_step("Processes (round-robin, preemptive)");

    /* ── 9. Network ─────────────────────────────────────────────────────── */
    net_init();
    boot_splash_step("Network (RTL8139 + TCP/IP + sockets)");

    /* ── 10. Filesystem ─────────────────────────────────────────────────── */
    fs_init();
    boot_splash_step("Filesystem (TinyFS, 64 inodes, 512 blocks)");

    /* ── 11. Tests ──────────────────────────────────────────────────────── */
    run_all_tests();
    boot_splash_step("Test suite (libc, fs, packets, memory)");

    /* ── 12. Syscalls + processes ───────────────────────────────────────── */
    isr_register_handler(0x80, syscall_handler);
    proc_create(process1_task, 1, PROC_MODE_KERNEL);
    proc_create(process2_task, 2, PROC_MODE_KERNEL);
    proc_create(shell_task, 3, PROC_MODE_KERNEL);
    proc_create(userprog_run, 4, PROC_MODE_KERNEL);
    boot_splash_step("Syscalls + 4 kernel processes");

    /* ── Ready ──────────────────────────────────────────────────────────── */
    boot_splash_done();

    /* ── Enable interrupts and launch GUI ───────────────────────────────── */
    extern void interrupts_enable(void);
    interrupts_enable();

    gfx_init();
    mouse_init();
    gui_init();
    gui_run();
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
