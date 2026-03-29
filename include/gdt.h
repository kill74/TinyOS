/* gdt.h — Global Descriptor Table */
#pragma once
#include <stdint.h>

void init_gdt(void);

/* Patch the base address of a GDT entry (used by init_tss). */
void gdt_set_base(int num, uint32_t base);
