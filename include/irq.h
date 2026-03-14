/* irq.h — Hardware IRQ management */
#pragma once
#include "idt.h"

void init_irq(void);

/* Register a C handler for a hardware IRQ line (0–15).
 * The handler is called from the IRQ dispatcher after the PIC is acknowledged.
 * Pass handler=NULL to unregister. */
void irq_register_handler(uint8_t irq, isr_t handler);

/* Unmask (enable) or mask (disable) a single IRQ line in the 8259 PIC.
 * irq must be in the range 0–15; values outside that range are ignored. */
void irq_enable(uint8_t irq);
void irq_disable(uint8_t irq);
