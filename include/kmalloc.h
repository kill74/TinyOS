/* kmalloc.h — Kernel heap allocator */
#pragma once
#include <stdint.h>
#include <stddef.h>

/* Initialise the heap over the given physical address range. */
void kmalloc_init(uint32_t start, uint32_t end);

/* Allocate `size` bytes. Returns a pointer aligned to 8 bytes.
 * Returns NULL on size==0. Halts the kernel if the heap is exhausted. */
void *kmalloc(size_t size);

/* Free a pointer previously returned by kmalloc().
 * Passing NULL is safe and does nothing.
 * Adjacent free blocks are coalesced automatically. */
void kfree(void *ptr);

/* Walk the entire heap and verify every block header/footer canary.
 * Prints a summary to VGA. Halts if corruption is detected.
 * Call this after any suspicious sequence of alloc/free operations. */
void kmalloc_check(void);

/* Print a brief heap usage summary (total / used / free / blocks). */
void kmalloc_stats(void);
