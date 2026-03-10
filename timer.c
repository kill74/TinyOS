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

#include "timer.h"
#include "irq.h"
#include "vga.h"
#include <stdint.h>

/* ── PIT I/O ports ────────────────────────────────────────────────────────── */
#define PIT_CHANNEL0  0x40   /* Channel 0 data port (connected to IRQ0)  */
#define PIT_CMD       0x43   /* PIT command/mode register                */

/* Command byte: channel 0, lo/hi byte access, mode 3 (square wave), binary */
#define PIT_CMD_BYTE  0x36

#define PIT_BASE_HZ   1193182UL

extern void outb(uint16_t port, uint8_t value);

/* ── Module state ─────────────────────────────────────────────────────────── */
static volatile uint32_t tick_count = 0;

/* ── IRQ0 handler ─────────────────────────────────────────────────────────── */
static void timer_callback(registers_t *regs) {
    (void)regs;          /* unused — suppress compiler warning */
    tick_count++;
}

/* ── Public API ───────────────────────────────────────────────────────────── */

/* Configure channel 0 of the PIT to fire at `frequency_hz` Hz. */
void init_timer(uint32_t frequency_hz) {
    /* Guard: a zero frequency would cause division-by-zero. Fall back to 1 Hz. */
    if (frequency_hz == 0) frequency_hz = 1;

    uint32_t divisor = PIT_BASE_HZ / frequency_hz;

    /* Send the command byte: select channel 0, set access mode, mode 3 */
    outb(PIT_CMD, PIT_CMD_BYTE);

    /* Send the 16-bit divisor in two bytes: low byte first, then high byte */
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));

    /* Register our callback for IRQ0 and unmask it in the PIC */
    irq_register_handler(0, timer_callback);
    irq_enable(0);  /* declared in irq.h, no local extern needed */
}

/* Return the number of timer ticks since boot. */
uint32_t timer_get_ticks(void) {
    return tick_count;
}
