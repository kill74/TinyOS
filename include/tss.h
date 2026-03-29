/* tss.h — Task State Segment */
#pragma once
#include <stdint.h>

#define TSS_SELECTOR 0x28

void init_tss(uint32_t kernel_esp);
