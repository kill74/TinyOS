/* kernel.c */
#include <stdint.h>

/* External initialization functions */
extern void init_idt();
extern void init_paging();

/* Simple VGA text mode writing */
void print_message(const char* message) {
    /* VGA Text buffer starts at physical address 0xB8000 */
    volatile uint16_t* vga_buffer = (uint16_t*)0xB8000;
    int index = 0;
    
    while(message[index] != '\0') {
        /* Character combined with color attributes (Light Grey on Black: 0x07) */
        vga_buffer[index] = (uint16_t)message[index] | (uint16_t)0x0700;
        index++;
    }
}

void kernel_main(void) {
    /* 1. Setup Interrupts */
    init_idt();

    /* 2. Setup Paging */
    init_paging();

    /* 3. Output a success message! */
    print_message("Tiny OS Booted! IDT and Paging Enabled.");

    /* Hang forever */
    while (1) {
        /* __asm__ __volatile__ ("hlt"); */
    }
}