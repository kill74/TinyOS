/* panic.h — Kernel panic handler */
#pragma once
#include "idt.h"

/* Halt the kernel with a formatted message and full diagnostic dump.
 * This function NEVER returns. */
__attribute__((noreturn)) void kernel_panic(const char *fmt, ...);

/* Halt the kernel after an unhandled CPU exception.
 * Called from isr_handler() when no specific handler is registered. */
__attribute__((noreturn)) void kernel_panic_exception(registers_t *regs);
