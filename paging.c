/* paging.c */
#include <stdint.h>

/* Page directory and table arrays must be 4KB aligned */
uint32_t page_directory[1024] __attribute__((aligned(4096)));
uint32_t first_page_table[1024] __attribute__((aligned(4096)));

extern void load_page_directory(uint32_t*);
extern void enable_paging();

void init_paging() {
    /* Set each entry to not present (Supervisor level, Read/Write, Not Present) */
    for(int i = 0; i < 1024; i++) {
        page_directory[i] = 0x00000002;
    }

    /* 
     * Identity map the first 4MB of memory.
     * Virtual address 0x0 to 0x3FFFFF will map to Physical 0x0 to 0x3FFFFF.
     */
    for(unsigned int i = 0; i < 1024; i++) {
        /* Address is page aligned (leaves 12 lowest bits for flags) */
        /* Flags: 3 = Present (1) | Read/Write (2) */
        first_page_table[i] = (i * 0x1000) | 3; 
    }

    /* Put the page table into the page directory */
    page_directory[0] = ((uint32_t)first_page_table) | 3;

    /* Give the CPU the address of our page directory and flip the paging bit! */
    load_page_directory(page_directory);
    enable_paging();
}