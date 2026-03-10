/* gdt.c — Global Descriptor Table (GDT)
 *
 * The GDT tells the CPU how to interpret segment registers (CS, DS, etc.).
 * In 32-bit protected mode we use a "flat" model: every segment covers the
 * full 4GB address space and protection is handled by paging, not segmentation.
 *
 * Our GDT has five entries:
 *   0 — Null descriptor   (required; CPU fault if CS/DS ever point here)
 *   1 — Kernel Code  0x08 (ring 0, execute/read)
 *   2 — Kernel Data  0x10 (ring 0, read/write)
 *   3 — User Code    0x18 (ring 3, execute/read)
 *   4 — User Data    0x20 (ring 3, read/write)
 */

#include "gdt.h"
#include <stdint.h>

/* ── Structures ───────────────────────────────────────────────────────────── */

/* A single 8-byte GDT entry (descriptor).
 * The layout is non-obvious because it evolved across CPU generations.
 * See Intel SDM Vol.3 §3.4.5 for the full picture. */
struct gdt_entry {
    uint16_t limit_low;    /* Bits 15:0  of the segment limit          */
    uint16_t base_low;     /* Bits 15:0  of the base address            */
    uint8_t  base_mid;     /* Bits 23:16 of the base address            */
    uint8_t  access;       /* Access byte (present, ring, type flags)   */
    uint8_t  granularity;  /* Bits 19:16 of limit + 4-bit flags         */
    uint8_t  base_high;    /* Bits 31:24 of the base address            */
} __attribute__((packed));

/* The 6-byte value loaded into GDTR (the GDT register). */
struct gdt_ptr {
    uint16_t limit;        /* Byte length of the GDT minus 1            */
    uint32_t base;         /* Linear address of the first GDT entry     */
} __attribute__((packed));

/* ── Module data ──────────────────────────────────────────────────────────── */
#define GDT_ENTRIES 5
static struct gdt_entry gdt[GDT_ENTRIES];
static struct gdt_ptr   gdtp;

extern void gdt_flush(uint32_t gdt_ptr_addr);   /* defined in helpers.S */

/* ── Helper: fill one descriptor ─────────────────────────────────────────── */
/*
 * access byte layout:
 *   bit 7   — Present        (1 = valid descriptor)
 *   bits 6:5 — DPL           (0 = kernel, 3 = user)
 *   bit 4   — Descriptor type (1 = code/data)
 *   bit 3   — Executable      (1 = code, 0 = data)
 *   bit 2   — Direction/Conforming
 *   bit 1   — Read/Write      (1 = allowed)
 *   bit 0   — Accessed        (CPU sets this; start at 0)
 *
 * granularity byte layout:
 *   bit 7   — Granularity    (1 = limit in 4KB pages, 0 = bytes)
 *   bit 6   — Size           (1 = 32-bit, 0 = 16-bit)
 *   bit 5   — Long mode      (0 for 32-bit)
 *   bit 4   — Available      (OS can use freely)
 *   bits 3:0 — Limit bits 19:16
 */
static void gdt_set_gate(int num,
                         uint32_t base, uint32_t limit,
                         uint8_t access, uint8_t gran) {
    gdt[num].base_low   = (uint16_t)(base & 0xFFFF);
    gdt[num].base_mid   = (uint8_t)((base >> 16) & 0xFF);
    gdt[num].base_high  = (uint8_t)((base >> 24) & 0xFF);

    gdt[num].limit_low  = (uint16_t)(limit & 0xFFFF);
    gdt[num].granularity = (uint8_t)(((limit >> 16) & 0x0F) | (gran & 0xF0));

    gdt[num].access     = access;
}

/* ── Public: initialise the GDT ──────────────────────────────────────────── */
void init_gdt(void) {
    gdtp.limit = (uint16_t)(sizeof(struct gdt_entry) * GDT_ENTRIES - 1);
    gdtp.base  = (uint32_t)&gdt;

    /* 0: Null — all fields zero */
    gdt_set_gate(0, 0, 0, 0, 0);

    /* 1: Kernel Code — base 0, limit 4GB, ring 0, executable */
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);
    /*           access 0x9A = 1001 1010
     *             Present=1, DPL=00, Type=1, Exec=1, RW=1, Accessed=0
     *           gran   0xCF = 1100 1111
     *             G=1 (4KB), 32-bit, limit high nibble=F               */

    /* 2: Kernel Data — base 0, limit 4GB, ring 0, writable */
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);
    /*           access 0x92 = 1001 0010  (same but not executable)     */

    /* 3: User Code — ring 3 (DPL=3), executable */
    gdt_set_gate(3, 0, 0xFFFFFFFF, 0xFA, 0xCF);
    /*           access 0xFA = 1111 1010  (DPL=11)                      */

    /* 4: User Data — ring 3, writable */
    gdt_set_gate(4, 0, 0xFFFFFFFF, 0xF2, 0xCF);

    gdt_flush((uint32_t)&gdtp);
}
