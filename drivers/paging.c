/* paging.c — x86 Paging / Memory Management Unit (MMU)
 *
 * x86 paging translates virtual addresses → physical addresses using a
 * two-level table hierarchy:
 *
 *   Virtual address (32-bit):
 *   ┌──────────┬──────────┬────────────┐
 *   │ Dir[9:0] │ Tbl[9:0] │ Offset[11:0]│
 *   └──────────┴──────────┴────────────┘
 *       10 bits     10 bits    12 bits
 *
 *   CR3 → Page Directory (1024 × 4-byte entries)
 *            ↓  (indexed by Dir bits)
 *          Page Table (1024 × 4-byte entries)
 *            ↓  (indexed by Tbl bits)
 *          Physical page (4 KB)
 *            ↓  (offset added directly)
 *          Physical byte
 *
 * Each entry's low 12 bits are flags; the upper 20 bits are a page frame number.
 *
 * Entry flags we use:
 *   bit 0 — Present   (1 = this entry is valid)
 *   bit 1 — Writable  (1 = writes allowed)
 *   bit 2 — User      (1 = user-mode accessible; 0 = kernel only)
 *
 * We identity-map the first 4 MB so that physical == virtual for all kernel
 * code and data. This lets us run without translating any existing addresses.
 */

#include "../include/paging.h"
#include "../include/log.h"
#include "../include/kmalloc.h"
#include <stdint.h>

/* Both arrays must be 4 KB-aligned — the CPU ignores the low 12 bits of CR3
 * and of each directory entry's page-table address field. */
uint32_t page_directory[1024] __attribute__((aligned(4096)));
uint32_t first_page_table[1024] __attribute__((aligned(4096)));
uint32_t kernel_page_table[1024] __attribute__((aligned(4096)));

extern void load_page_directory(uint32_t *dir);
extern void enable_paging(void);

void map_page(uint32_t virt, uint32_t phys, uint32_t flags)
{
    uint32_t dir_idx = virt >> 22;
    uint32_t tbl_idx = (virt >> 12) & 0x3FF;

    /* If the directory entry is not present, allocate a new page table */
    if (!(page_directory[dir_idx] & 0x1)) {
        uint32_t *new_table = (uint32_t *)kmalloc(4096);
        if (!new_table) {
            LOG_ERROR("map_page: out of memory for page table");
            return;
        }
        for (int i = 0; i < 1024; i++)
            new_table[i] = 0x00000002; /* not present */
        page_directory[dir_idx] = ((uint32_t)new_table) | 0x7; /* P|W|U */
    }

    uint32_t *table = (uint32_t *)(page_directory[dir_idx] & ~0xFFF);
    table[tbl_idx] = (phys & ~0xFFF) | (flags & 0xFFF);
    __asm__ __volatile__("invlpg (%0)" : : "r"(virt) : "memory");
}

void init_paging(void)
{
    LOG_INFO("Initializing paging");
    /* ── Step 1: mark all 1024 directory entries as Not Present ─────────── */
    /* 0x00000002 = Supervisor | Read/Write | NOT Present
     * The Not-Present bit means the CPU will fault if any address in that
     * 4 MB region is accessed — safe until we explicitly map something. */
    for (int i = 0; i < 1024; i++)
    {
        page_directory[i] = 0x00000002;
    }

    /* ── Step 2: build a kernel-only identity page table for >= KERNEL_VIRTUAL_BASE */
    /* We'll use first_page_table for kernel region mapping */
    for (uint32_t i = 0; i < 1024; i++)
    {
        uint32_t phys_addr = i * 0x400000;   /* each entry maps 4 MB */
        if (phys_addr >= KERNEL_VIRTUAL_BASE)
        {
            /* Map kernel region as present+writable (no user) */
            first_page_table[i] = phys_addr | 0x3;
        }
        else
        {
            /* Leave low region not present – will be filled in proc_create */
            first_page_table[i] = 0x00000002;
        }
    }

    /* ── Step 3: point directory entry 0 at our page table ───────────────── */
    /* The physical address of `first_page_table` must be OR-ed with the same
     * flags: Present | Writable. The CPU strips the low 12 bits when it reads
     * the table base address. */
    page_directory[0] = ((uint32_t)first_page_table) | 0x3;

    /* ── Step 4: load CR3 and turn on the PG bit in CR0 ─────────────────── */
    load_page_directory(page_directory);
    enable_paging();
    LOG_INFO("Paging initialized (kernel region mapped, user region unmapped)");
}
