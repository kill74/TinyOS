/* idt.h — Interrupt Descriptor Table */
#pragma once
#include <stdint.h>

/* The register state saved by isr.S before calling our C handlers.
 * Passed by pointer to isr_handler() and irq_handler(). */
typedef struct {
    /* Saved by pusha (in reverse order: EDI pushed first) */
    uint32_t edi, esi, ebp, esp_dummy;
    uint32_t ebx, edx, ecx, eax;

    /* Pushed by our stubs */
    uint32_t ds;
    uint32_t int_no;    /* interrupt / exception number */
    uint32_t err_code;  /* error code (or 0 if none)    */

    /* Pushed automatically by the CPU on interrupt entry */
    uint32_t eip, cs, eflags, user_esp, user_ss;
} registers_t;

/* Register a C function as the handler for a specific interrupt number. */
typedef void (*isr_t)(registers_t *);
void isr_register_handler(uint8_t num, isr_t handler);

void init_idt(void);
