/* elf.c — ELF binary loader
 *
 * Loads a flat ELF binary (no dynamic linking) into the current process's
 * address space. Only PT_LOAD segments are supported. BSS (memsz > filesz)
 * is zero-filled.
 *
 * Returns 0 on success, -1 on failure.
 */

#include "../include/elf.h"
#include "../include/log.h"
#include "../include/kmalloc.h"
#include "../include/paging.h"
#include <stdint.h>
#include <stddef.h>

/* ── Helpers ───────────────────────────────────────────────────────────── */
static void elf_memset(void *dst, int val, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    while (n--)
        *d++ = (uint8_t)val;
}

static void elf_memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--)
        *d++ = *s++;
}

/* ── Public API ────────────────────────────────────────────────────────── */
int elf_load(const uint8_t *data, size_t size, uint32_t *entry_out)
{
    /* ── 1. Validate ELF header ────────────────────────────────────────── */
    if (size < sizeof(elf_header_t)) {
        LOG_ERROR("ELF: data too small for header (%u bytes)", size);
        return -1;
    }

    const elf_header_t *hdr = (const elf_header_t *)data;

    uint32_t magic = (uint32_t)hdr->e_ident[0]
                   | ((uint32_t)hdr->e_ident[1] << 8)
                   | ((uint32_t)hdr->e_ident[2] << 16)
                   | ((uint32_t)hdr->e_ident[3] << 24);
    if (magic != ELF_MAGIC) {
        LOG_ERROR("ELF: bad magic 0x%x (expected 0x%x)", magic, ELF_MAGIC);
        return -1;
    }

    /* Class: 1 = 32-bit */
    if (hdr->e_ident[4] != 1) {
        LOG_ERROR("ELF: not a 32-bit binary (class=%d)", hdr->e_ident[4]);
        return -1;
    }

    /* Encoding: 1 = little-endian */
    if (hdr->e_ident[5] != 1) {
        LOG_ERROR("ELF: not little-endian");
        return -1;
    }

    /* Machine: 3 = x86 */
    if (hdr->e_machine != 3) {
        LOG_ERROR("ELF: unsupported machine 0x%x", hdr->e_machine);
        return -1;
    }

    if (hdr->e_phoff == 0 || hdr->e_phnum == 0) {
        LOG_ERROR("ELF: no program headers");
        return -1;
    }

    if (hdr->e_phentsize != sizeof(elf_program_header_t)) {
        LOG_ERROR("ELF: unexpected phentsize %u (expected %u)",
                  hdr->e_phentsize, (uint32_t)sizeof(elf_program_header_t));
        return -1;
    }

    LOG_INFO("ELF: entry=0x%x, %u program headers, type=%u",
             hdr->e_entry, hdr->e_phnum, hdr->e_type);

    /* ── 2. Load PT_LOAD segments ──────────────────────────────────────── */
    for (uint16_t i = 0; i < hdr->e_phnum; i++) {
        const elf_program_header_t *ph =
            (const elf_program_header_t *)(data + hdr->e_phoff
                                           + i * hdr->e_phentsize);

        if (ph->p_type != ELF_PT_LOAD)
            continue;

        /* Sanity: segment must lie within the file */
        if (ph->p_offset + ph->p_filesz > size) {
            LOG_ERROR("ELF: segment %u extends past end of file", i);
            return -1;
        }

        /* Sanity: destination must be below kernel space */
        if (ph->p_vaddr >= KERNEL_VIRTUAL_BASE) {
            LOG_ERROR("ELF: segment %u vaddr 0x%x in kernel space", i,
                      ph->p_vaddr);
            return -1;
        }

        LOG_INFO("ELF: seg %u vaddr=0x%x filesz=%u memsz=%u flags=0x%x",
                 i, ph->p_vaddr, ph->p_filesz, ph->p_memsz, ph->p_flags);

        /* Allocate physical pages for this segment */
        uint32_t seg_start = ph->p_vaddr & ~0xFFF;
        uint32_t seg_end =
            (ph->p_vaddr + ph->p_memsz + 0xFFF) & ~0xFFF;
        uint32_t npages = (seg_end - seg_start) / 4096;

        for (uint32_t pg = 0; pg < npages; pg++) {
            uint32_t vaddr = seg_start + pg * 4096;
            void *page = kmalloc(4096);
            if (!page) {
                LOG_ERROR("ELF: out of memory mapping page at 0x%x",
                          vaddr);
                return -1;
            }
            /* Map user-accessible, writable if segment is writable */
            uint32_t flags = 0x5; /* Present | User */
            if (ph->p_flags & ELF_PF_W)
                flags |= 0x2;    /* Writable */
            map_page(vaddr, (uint32_t)page, flags);
        }

        /* Copy file data into the mapped pages */
        elf_memcpy((void *)ph->p_vaddr, data + ph->p_offset,
                   ph->p_filesz);

        /* Zero-fill BSS (memsz > filesz) */
        if (ph->p_memsz > ph->p_filesz) {
            elf_memset((void *)(ph->p_vaddr + ph->p_filesz), 0,
                       ph->p_memsz - ph->p_filesz);
        }
    }

    *entry_out = hdr->e_entry;
    LOG_INFO("ELF: loaded successfully, entry=0x%x", hdr->e_entry);
    return 0;
}
