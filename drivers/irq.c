/* irq.c — 8259 Programmable Interrupt Controller (PIC) + IRQ dispatch
 *
 * The PC/AT has two cascaded 8259 PICs (master + slave) that translate
 * hardware IRQ lines into CPU interrupt vectors. Out of the box they fire
 * on vectors 8–15 and 0x70–0x77, which clash with CPU exceptions (0–31).
 *
 * We remap them to vectors 32–47 so hardware IRQs don't look like exceptions.
 *
 * After each IRQ the CPU expects an "End Of Interrupt" (EOI) command sent
 * back to the PIC, otherwise it will never fire that IRQ line again.
 */

#include "../include/irq.h"
#include "../include/idt.h"
#include "../include/log.h"
#include <stdint.h>

/* ── PIC I/O port addresses ───────────────────────────────────────────────── */
#define PIC1_CMD    0x20    /* Master PIC command port   */
#define PIC1_DATA   0x21    /* Master PIC data port      */
#define PIC2_CMD    0xA0    /* Slave  PIC command port   */
#define PIC2_DATA   0xA1    /* Slave  PIC data port      */

#define PIC_EOI     0x20    /* "End Of Interrupt" command byte */

/* Initialisation Control Words (ICWs) */
#define ICW1_INIT   0x10    /* Begin initialisation sequence  */
#define ICW1_ICW4   0x01    /* Signals that ICW4 will follow  */
#define ICW4_8086   0x01    /* 8086/88 mode (not MCS-80/85)   */

/* Where to map the two PICs in the IDT */
#define PIC1_OFFSET 32      /* Master maps to vectors 32–39  */
#define PIC2_OFFSET 40      /* Slave  maps to vectors 40–47  */

extern void    outb(uint16_t port, uint8_t value);
extern uint8_t inb(uint16_t port);
extern void    io_wait(void);

/* ── Handler dispatch table (one slot per IRQ line 0–15) ─────────────────── */
static isr_t irq_handlers[16];

void irq_register_handler(uint8_t irq, isr_t handler) {
    /* Silently ignore out-of-range values — better than corrupting memory. */
    if (irq >= 16) return;
    irq_handlers[irq] = handler;
}

/* ── Remap the 8259 PICs ──────────────────────────────────────────────────── */
/* This is a 4-step handshake with each PIC. `io_wait()` inserts tiny delays
 * that are necessary on real hardware to let the PIC process each command. */
static void pic_remap(void) {
    /* Save current masks so drivers haven't lost their settings on remap */
    uint8_t mask1 = inb(PIC1_DATA);
    uint8_t mask2 = inb(PIC2_DATA);

    /* ICW1: start initialisation (edge-triggered, cascade mode) */
    outb(PIC1_CMD, ICW1_INIT | ICW1_ICW4); io_wait();
    outb(PIC2_CMD, ICW1_INIT | ICW1_ICW4); io_wait();

    /* ICW2: tell each PIC its new vector offset */
    outb(PIC1_DATA, PIC1_OFFSET); io_wait();
    outb(PIC2_DATA, PIC2_OFFSET); io_wait();

    /* ICW3: tell master that slave is on IRQ2, tell slave its cascade ID */
    outb(PIC1_DATA, 0x04); io_wait();   /* Master: slave on pin 2 (bit mask) */
    outb(PIC2_DATA, 0x02); io_wait();   /* Slave:  cascade identity = 2      */

    /* ICW4: 8086 mode */
    outb(PIC1_DATA, ICW4_8086); io_wait();
    outb(PIC2_DATA, ICW4_8086); io_wait();

    /* Restore saved masks */
    outb(PIC1_DATA, mask1);
    outb(PIC2_DATA, mask2);
}

/* Send End-Of-Interrupt to the correct PIC(s). */
static void pic_eoi(uint8_t irq) {
    if (irq >= 8) {
        outb(PIC2_CMD, PIC_EOI);    /* Must also tell the slave */
    }
    outb(PIC1_CMD, PIC_EOI);
}

/* ── C-level IRQ dispatcher ───────────────────────────────────────────────── */
/* Called from irq_common in isr.S. int_no is the IDT vector (32–47);
 * we convert to IRQ line 0–15 by subtracting the PIC1_OFFSET. */
void irq_handler(registers_t *regs) {
    /* Defensive bounds check: the stubs always push 32–47, but be explicit. */
    if (regs->int_no < PIC1_OFFSET || regs->int_no >= PIC1_OFFSET + 16) {
        /* Should never happen. Send EOI and return — don't crash the system. */
        outb(PIC1_CMD, PIC_EOI);
        return;
    }

    uint8_t irq = (uint8_t)(regs->int_no - PIC1_OFFSET);

    if (irq_handlers[irq]) {
        irq_handlers[irq](regs);
    }

    /* Always send EOI — even if we had no handler, the PIC must be released. */
    pic_eoi(irq);
}

/* ── Public: initialise IRQ subsystem ────────────────────────────────────── */
void init_irq(void) {
    LOG_INFO("Initializing IRQ controller");
    pic_remap();

    /* Start with all IRQs masked (disabled).
     * Drivers unmask their own line when they initialise.
     * 0xFF = all masked, 0x00 = all enabled. */
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
    LOG_INFO("IRQ controller initialized");
}

/* Enable (unmask) a single IRQ line. */
void irq_enable(uint8_t irq) {
    if (irq >= 16) return;  /* guard: only lines 0–15 exist */
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    uint8_t  bit  = (irq < 8) ? irq : (uint8_t)(irq - 8);
    outb(port, inb(port) & (uint8_t)~(1u << bit));
}

/* Disable (mask) a single IRQ line. */
void irq_disable(uint8_t irq) {
    if (irq >= 16) return;
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    uint8_t  bit  = (irq < 8) ? irq : (uint8_t)(irq - 8);
    outb(port, inb(port) | (uint8_t)(1u << bit));
}
