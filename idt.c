/* idt.c */
#include <stdint.h>

/* Defines an IDT entry */
struct idt_entry {
    uint16_t base_low;
    uint16_t sel;
    uint8_t  always0;
    uint8_t  flags;
    uint16_t base_high;
} __attribute__((packed));

/* Defines the pointer to the IDT array (loaded via 'lidt') */
struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

struct idt_entry idt[256];
struct idt_ptr idtp;

extern void idt_load(uint32_t idt_ptr_addr);

/* Function to add an interrupt handler to the IDT */
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_low = (base & 0xFFFF);
    idt[num].base_high = (base >> 16) & 0xFFFF;
    idt[num].sel = sel;         /* Kernel Code Segment selector (usually 0x08) */
    idt[num].always0 = 0;
    idt[num].flags = flags;     /* e.g., 0x8E for 32-bit interrupt gate */
}

void init_idt() {
    idtp.limit = (sizeof(struct idt_entry) * 256) - 1;
    idtp.base = (uint32_t)&idt;

    /* Clear out the entire IDT, initializing it to zeros */
    for (int i = 0; i < 256; i++) {
        idt_set_gate(i, 0, 0, 0);
    }

    /* Normally, here you would add ISRs (Interrupt Service Routines) */
    // idt_set_gate(33, (uint32_t)keyboard_handler, 0x08, 0x8E);

    /* Load the IDT into the processor */
    idt_load((uint32_t)&idtp);
}