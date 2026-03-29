/* timer.c — Programmable Interval Timer (PIT) driver
 *
 * The PIT (Intel 8253/8254) has a fixed input clock of 1,193,182 Hz.
 * We program it to fire IRQ0 at a desired frequency by setting a divisor:
 *
 *   divisor = PIT_BASE_HZ / desired_frequency
 *
 * Each IRQ0 increments a global tick counter that the rest of the kernel
 * can read to measure elapsed time.
 */

#include "../include/timer.h"
#include "../include/irq.h"
#include "../include/vga.h"
#include "../include/log.h"
#include "../include/process.h"
#include "../net/rtl8139.h"
#include "../net/tcp.h"
#include "../net/arp.h"
#include "../gui/gui.h"
#include <stdint.h>

/* ── PIT I/O ports ────────────────────────────────────────────────────────── */
#define PIT_CHANNEL0 0x40 /* Channel 0 data port (connected to IRQ0)  */
#define PIT_CMD 0x43      /* PIT command/mode register                */

/* Command byte: channel 0, lo/hi byte access, mode 3 (square wave), binary */
#define PIT_CMD_BYTE 0x36

#define PIT_BASE_HZ 1193182UL

extern void outb(uint16_t port, uint8_t value);

/* ── Module state ─────────────────────────────────────────────────────────── */
static volatile uint32_t tick_count = 0;

/* ── IRQ0 handler ─────────────────────────────────────────────────────────── */
static void timer_callback(registers_t *regs)
{
    (void)regs;
    tick_count++;
    proc_tick(tick_count);

    /* Network: poll NIC for received packets */
    rtl8139_poll();

    /* Network: TCP retransmissions and timers */
    tcp_tick();

    /* Network: age ARP entries (every ~1 second) */
    if ((tick_count % 100) == 0)
        arp_tick();

    /* GUI: update clock and trigger redraw */
    gui_tick();

    /* Preemptive scheduling: force a context switch on every tick */
    preempt();
}

/* ── Public API ───────────────────────────────────────────────────────────── */

/* Configure channel 0 of the PIT to fire at `frequency_hz` Hz. */
void init_timer(uint32_t frequency_hz)
{
    LOG_INFO("Initializing timer at %u Hz", frequency_hz);
    /* Guard: a zero frequency would cause division-by-zero. Fall back to 1 Hz. */
    if (frequency_hz == 0)
        frequency_hz = 1;

    uint32_t divisor = PIT_BASE_HZ / frequency_hz;

    /* Send the command byte: select channel 0, set access mode, mode 3 */
    outb(PIT_CMD, PIT_CMD_BYTE);

    /* Send the 16-bit divisor in two bytes: low byte first, then high byte */
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));

    /* Register our callback for IRQ0 and unmask it in the PIC */
    irq_register_handler(0, timer_callback);
    irq_enable(0); /* declared in irq.h, no local extern needed */
    LOG_INFO("Timer initialized (divisor: %u)", divisor);
}

/* Return the number of timer ticks since boot. */
uint32_t timer_get_ticks(void)
{
    return tick_count;
}
