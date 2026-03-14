/* kmalloc.c — Free-list kernel heap allocator
 *
 * Layout
 * ──────
 * The heap is one contiguous region divided into variable-size blocks.
 * Every block has a header and a footer so we can walk the list forward
 * and coalesce free neighbours efficiently.
 *
 *   ┌──────────────┐  ← block_header_t
 *   │  magic       │    canary: MAGIC_FREE or MAGIC_USED
 *   │  size        │    payload bytes (NOT including header+footer)
 *   │  free        │    1 = free, 0 = allocated
 *   ├──────────────┤
 *   │  payload...  │  ← pointer returned to the caller
 *   ├──────────────┤
 *   │  footer_magic│  ← block_footer_t  (must mirror header magic)
 *   └──────────────┘
 *
 * Allocation  — first-fit search; split the block if a useful remainder
 *               would be left over.
 *
 * Freeing     — mark free; coalesce with next block if it is also free;
 *               then coalesce with previous block if also free.
 *               Adjacent free blocks are always merged, keeping the list
 *               as compact as possible and preventing fragmentation.
 *
 * Canaries    — header AND footer store a magic value that changes between
 *               MAGIC_FREE and MAGIC_USED on every alloc/free.
 *               kmalloc_check() walks every block and verifies both canaries,
 *               catching heap overflows, double-frees, and wild writes.
 */

#include "../include/kmalloc.h"
#include "../include/vga.h"
#include "../include/log.h"
#include <stdint.h>
#include <stddef.h>

/* ── Canary constants ─────────────────────────────────────────────────────── */
#define MAGIC_FREE   0xDEADBEEFu   /* written into header+footer when free */
#define MAGIC_USED   0xCAFEBABEu   /* written into header+footer when used */

/* ── Alignment ────────────────────────────────────────────────────────────── */
#define ALIGN        8u
#define ALIGN_UP(x)  (((size_t)(x) + (ALIGN - 1u)) & ~(ALIGN - 1u))

/* Minimum payload that makes splitting a block worthwhile */
#define MIN_SPLIT    16u

/* ── Block metadata structures ────────────────────────────────────────────── */
typedef struct {
    uint32_t magic;    /* MAGIC_FREE or MAGIC_USED                  */
    size_t   size;     /* payload bytes (NOT including header+footer) */
    uint8_t  free;     /* 1 = free, 0 = in use                      */
} block_header_t;

typedef struct {
    uint32_t magic;    /* must always match the header magic         */
} block_footer_t;

/* Sizes rounded up to alignment boundary */
#define HDR_SZ   ALIGN_UP(sizeof(block_header_t))
#define FTR_SZ   ALIGN_UP(sizeof(block_footer_t))
#define META_SZ  (HDR_SZ + FTR_SZ)   /* per-block overhead */

/* ── Heap bounds (set once by kmalloc_init) ───────────────────────────────── */
static uint8_t *heap_start = (uint8_t *)0;
static uint8_t *heap_end   = (uint8_t *)0;

/* ── Navigation ───────────────────────────────────────────────────────────── */

static inline void *hdr_to_payload(block_header_t *h) {
    return (void *)((uint8_t *)h + HDR_SZ);
}

static inline block_header_t *payload_to_hdr(void *p) {
    return (block_header_t *)((uint8_t *)p - HDR_SZ);
}

static inline block_footer_t *hdr_to_footer(block_header_t *h) {
    return (block_footer_t *)((uint8_t *)h + HDR_SZ + h->size);
}

/* Return the next block in the heap, or NULL if at the end. */
static inline block_header_t *next_block(block_header_t *h) {
    uint8_t *next_addr = (uint8_t *)h + HDR_SZ + h->size + FTR_SZ;
    if (next_addr >= heap_end) return (block_header_t *)0;
    return (block_header_t *)next_addr;
}

/* ── Internal: write both canaries for a block ────────────────────────────── */
static void block_write(block_header_t *h, size_t size, uint8_t free) {
    uint32_t magic = free ? MAGIC_FREE : MAGIC_USED;
    h->magic = magic;
    h->size  = size;
    h->free  = free;
    hdr_to_footer(h)->magic = magic;   /* footer must always mirror header */
}

/* ── Halt helper ─────────────────────────────────────────────────────────── */
static void kernel_panic(void) {
    __asm__ __volatile__("cli; hlt");
    /* unreachable — silences compiler "no return" warnings */
    while (1) {}
}

/* ── Public: initialise ───────────────────────────────────────────────────── */
void kmalloc_init(uint32_t start, uint32_t end) {
    LOG_INFO("Initializing heap (start: 0x%x, end: 0x%x)", start, end);
    heap_start = (uint8_t *)ALIGN_UP(start);
    heap_end   = (uint8_t *)end;

    /* The entire heap begins as one large free block. */
    size_t payload = (size_t)(heap_end - heap_start) - META_SZ;
    block_write((block_header_t *)heap_start, payload, 1);
    LOG_INFO("Heap initialized with %u bytes available", payload);
}

/* ── Public: allocate ─────────────────────────────────────────────────────── */
void *kmalloc(size_t size) {
    if (size == 0) return (void *)0;

    size = ALIGN_UP(size);   /* keep all payloads aligned */

    /* First-fit: walk the block list until we find a free block big enough. */
    block_header_t *cur = (block_header_t *)heap_start;
    while (cur) {
        if (cur->free && cur->size >= size) break;
        cur = next_block(cur);
    }

    if (!cur) {
        vga_set_color(VGA_WHITE, VGA_RED);
        vga_printf("\n[kmalloc] OUT OF MEMORY (need %u bytes)\n", (unsigned)size);
        kernel_panic();
        return (void *)0;
    }

    /* Split only when the remainder is large enough to be useful. */
    size_t remainder = cur->size - size;
    if (remainder >= META_SZ + MIN_SPLIT) {
        /* Remainder block starts immediately after the payload+footer of `cur`. */
        block_header_t *split =
            (block_header_t *)((uint8_t *)cur + HDR_SZ + size + FTR_SZ);
        block_write(split, remainder - META_SZ, 1);
        block_write(cur, size, 0);
    } else {
        /* Use the whole block; internal fragmentation but no tiny orphans. */
        block_write(cur, cur->size, 0);
    }

    return hdr_to_payload(cur);
}

/* ── Public: free ─────────────────────────────────────────────────────────── */
void kfree(void *ptr) {
    if (!ptr) return;   /* freeing NULL is always a no-op */

    block_header_t *h = payload_to_hdr(ptr);

    /* ── Canary check: detect double-free and wild-pointer free ─────────── */
    if (h->magic != MAGIC_USED) {
        vga_set_color(VGA_WHITE, VGA_RED);
        if (h->magic == MAGIC_FREE)
            vga_printf("\n[kfree] DOUBLE FREE at 0x%x\n", (unsigned)(uint32_t)ptr);
        else
            vga_printf("\n[kfree] INVALID PTR 0x%x (magic=0x%x)\n",
                       (unsigned)(uint32_t)ptr, h->magic);
        kernel_panic();
        return;
    }

    /* Mark block as free. */
    block_write(h, h->size, 1);

    /* ── Coalesce forward: merge with the next block if it is free ───────── */
    block_header_t *next = next_block(h);
    if (next && next->free) {
        /* Our new payload size absorbs next's payload AND its metadata. */
        block_write(h, h->size + META_SZ + next->size, 1);
        /* `next` is now unreachable — the new footer covers it. */
    }

    /* ── Coalesce backward: merge with the previous block if it is free ──── *
     * We walk from heap_start to find it. This is O(n) in the number of
     * blocks, which is acceptable for a kernel heap of this size.           */
    if ((uint8_t *)h > heap_start) {
        block_header_t *prev = (block_header_t *)heap_start;
        block_header_t *scan_next;
        while ((scan_next = next_block(prev)) && scan_next != h) {
            prev = scan_next;
        }
        if (prev != h && prev->free) {
            block_write(prev, prev->size + META_SZ + h->size, 1);
        }
    }
}

/* ── Public: integrity check ─────────────────────────────────────────────── */
void kmalloc_check(void) {
    uint32_t n = 0, bad = 0;

    block_header_t *cur = (block_header_t *)heap_start;
    while (cur) {
        n++;
        uint32_t expected  = cur->free ? MAGIC_FREE : MAGIC_USED;
        uint32_t ftr_magic = hdr_to_footer(cur)->magic;

        if (cur->magic != expected || ftr_magic != expected) {
            vga_set_color(VGA_WHITE, VGA_RED);
            vga_printf("\n[kmalloc_check] block %u CORRUPT "
                       "(hdr=0x%x ftr=0x%x expected=0x%x)\n",
                       n, cur->magic, ftr_magic, expected);
            bad++;
        }
        cur = next_block(cur);
    }

    if (bad) kernel_panic();
}

/* ── Public: statistics ───────────────────────────────────────────────────── */
void kmalloc_stats(void) {
    size_t   heap_total  = (size_t)(heap_end - heap_start);
    size_t   used_bytes  = 0, free_bytes = 0;
    uint32_t used_blocks = 0, free_blocks = 0;

    block_header_t *cur = (block_header_t *)heap_start;
    while (cur) {
        if (cur->free) { free_bytes += cur->size; free_blocks++; }
        else           { used_bytes += cur->size; used_blocks++;  }
        cur = next_block(cur);
    }

    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_printf("  HEAP %u B total | used: %u B (%u blocks)"
               " | free: %u B (%u blocks)\n",
               (unsigned)heap_total,
               (unsigned)used_bytes,  used_blocks,
               (unsigned)free_bytes,  free_blocks);
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}
