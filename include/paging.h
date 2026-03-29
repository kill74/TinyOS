/* paging.h */
#pragma once
#include <stdint.h>
#define KERNEL_VIRTUAL_BASE 0xC0000000UL
void init_paging(void);
void load_page_directory(uint32_t *dir);
void enable_paging(void);
void map_page(uint32_t virt, uint32_t phys, uint32_t flags);
