/* paging.h */
#pragma once
#include <stdint.h>
void init_paging(void);
/* Per-processor paging control (Phase B) */
void load_page_directory(uint32_t *dir);
void enable_paging(void);
