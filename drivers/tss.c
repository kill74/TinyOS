/* tss.c — Task State Segment
 *
 * The TSS tells the CPU which stack to switch to (SS0:ESP0) when an
 * interrupt fires while running in ring 3. Without it, int 0x80 and
 * hardware IRQs from user mode would use the user stack — corrupting it.
 */

#include "../include/tss.h"
#include "../include/gdt.h"
#include "../include/log.h"
#include <stdint.h>

/* ── TSS structure (104 bytes) ─────────────────────────────────────────── */
typedef struct {
    uint32_t prev_tss;
    uint32_t esp0;
    uint32_t ss0;
    uint32_t esp1, ss1;
    uint32_t esp2, ss2;
    uint32_t cr3, eip, eflags;
    uint32_t eax, ecx, edx, ebx;
    uint32_t esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;
} __attribute__((packed)) tss_entry_t;

static tss_entry_t tss;

/* ── Assembly: load the Task Register ──────────────────────────────────── */
static void tr_load(uint16_t sel)
{
    __asm__ __volatile__("ltr %0" : : "r"(sel));
}

/* ── Public: initialise TSS and load TR ────────────────────────────────── */
void init_tss(uint32_t kernel_esp)
{
    LOG_INFO("Initializing TSS");

    /* Zero the entire structure */
    for (uint32_t *p = (uint32_t *)&tss;
         p < (uint32_t *)&tss + sizeof(tss) / 4; p++)
        *p = 0;

    tss.ss0 = 0x10; /* kernel data segment */
    tss.esp0 = kernel_esp;
    tss.iomap_base = sizeof(tss); /* no I/O bitmap */

    /* Patch the TSS descriptor (GDT entry 5) with the real address */
    gdt_set_base(5, (uint32_t)&tss);

    /* Load the Task Register with selector 0x28 (entry 5 * 8) */
    tr_load(TSS_SELECTOR);

    LOG_INFO("TSS initialized (kernel ESP0=0x%x)", kernel_esp);
}
