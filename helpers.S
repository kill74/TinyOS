/* helpers.S — Low-level CPU helper routines
 *
 * These are thin assembly wrappers around CPU instructions that cannot be
 * expressed in C. Each function is declared `extern` in the relevant C header
 * and called normally from C code.
 */

.section .text

/* ── GDT ──────────────────────────────────────────────────────────────────── */

/* gdt_flush(uint32_t gdt_ptr_addr)
 * Load the new GDT and reload all segment registers.
 * After loading a new GDT the CPU's segment register caches are stale —
 * we must reload CS (via a far jump) and all data segment registers. */
.global gdt_flush
gdt_flush:
    mov  4(%esp), %eax             /* First argument: address of our gdt_ptr */
    lgdt (%eax)                    /* Load the GDT */

    /* Reload data segments with the Kernel Data selector (GDT entry 2 = 0x10) */
    mov  $0x10, %ax
    mov  %ax, %ds
    mov  %ax, %es
    mov  %ax, %fs
    mov  %ax, %gs
    mov  %ax, %ss

    /* Far jump to reload CS with the Kernel Code selector (GDT entry 1 = 0x08).
     * A long jump flushes the instruction pipeline and the CS shadow register. */
    ljmp $0x08, $.Lgdt_done
.Lgdt_done:
    ret

/* ── IDT ──────────────────────────────────────────────────────────────────── */

/* idt_load(uint32_t idt_ptr_addr)
 * Tell the CPU where our Interrupt Descriptor Table lives. */
.global idt_load
idt_load:
    mov  4(%esp), %eax
    lidt (%eax)
    ret

/* ── Paging ───────────────────────────────────────────────────────────────── */

/* load_page_directory(uint32_t* page_dir)
 * Write the physical address of the page directory into CR3.
 * The CPU uses CR3 to find the page directory on every memory access. */
.global load_page_directory
load_page_directory:
    mov  4(%esp), %eax
    mov  %eax, %cr3
    ret

/* enable_paging()
 * Set bit 31 (PG) in CR0 to activate the Memory Management Unit. */
.global enable_paging
enable_paging:
    mov  %cr0, %eax
    or   $0x80000000, %eax         /* Set PG bit */
    mov  %eax, %cr0
    ret

/* ── I/O Ports ────────────────────────────────────────────────────────────── */

/* outb(uint16_t port, uint8_t value)
 * Write one byte to an I/O port (used for PIC, PIT, keyboard controller). */
.global outb
outb:
    mov  4(%esp), %edx             /* port */
    mov  8(%esp), %eax             /* value */
    out  %al, %dx
    ret

/* inb(uint16_t port) -> uint8_t
 * Read one byte from an I/O port. */
.global inb
inb:
    mov  4(%esp), %edx             /* port */
    xor  %eax, %eax
    in   %dx, %al                  /* Result goes into AL (low byte of EAX) */
    ret

/* io_wait()
 * Insert a tiny delay by writing to an unused port.
 * Required after some PIC/PIT commands on older hardware. */
.global io_wait
io_wait:
    out  %al, $0x80
    ret

/* outw(uint16_t port, uint16_t value)
 * Write one word (16 bits) to an I/O port. */
.global outw
outw:
    mov  4(%esp), %edx             /* port */
    mov  8(%esp), %eax             /* value */
    out  %ax, %dx
    ret

/* inw(uint16_t port) -> uint16_t
 * Read one word (16 bits) from an I/O port. */
.global inw
inw:
    mov  4(%esp), %edx             /* port */
    xor  %eax, %eax
    in   %dx, %ax
    ret

/* outl(uint16_t port, uint32_t value)
 * Write one dword (32 bits) to an I/O port. */
.global outl
outl:
    mov  4(%esp), %edx             /* port */
    mov  8(%esp), %eax             /* value */
    out  %eax, %dx
    ret

/* inl(uint16_t port) -> uint32_t
 * Read one dword (32 bits) from an I/O port. */
.global inl
inl:
    mov  4(%esp), %edx             /* port */
    in   %dx, %eax
    ret

/* ── Interrupt control ────────────────────────────────────────────────────── */

.global interrupts_enable
interrupts_enable:
    sti
    ret

.global interrupts_disable
interrupts_disable:
    cli
    ret
