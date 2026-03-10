/* boot.S — Multiboot-compliant entry point
 *
 * This is the very first code that runs when the bootloader (GRUB/QEMU)
 * hands control to our kernel. Its only job is to:
 *   1. Announce itself as a valid Multiboot kernel (magic header).
 *   2. Set up a small stack so C code can run.
 *   3. Jump into kernel_main().
 */

.set MAGIC,    0x1BADB002          /* Multiboot magic number — bootloader looks for this */
.set FLAGS,    0x03                /* Flags: align modules on page boundaries + give us a memory map */
.set CHECKSUM, -(MAGIC + FLAGS)    /* Checksum: MAGIC + FLAGS + CHECKSUM must equal 0 */

/* ── Multiboot Header ─────────────────────────────────────────────────────── */
.section .multiboot
.align 4
    .long MAGIC
    .long FLAGS
    .long CHECKSUM

/* ── Stack ────────────────────────────────────────────────────────────────── */
/* C requires a stack before any function can be called.
 * We reserve 16 KB in BSS (zero-initialised, doesn't bloat the binary). */
.section .bss
.align 16
stack_bottom:
    .skip 16384                    /* 16 KB stack */
stack_top:

/* ── Entry Point ──────────────────────────────────────────────────────────── */
.section .text
.global _start
.type _start, @function
_start:
    /* Point the stack pointer at the top of our reserved stack area */
    mov  $stack_top, %esp

    /* Call the kernel — this should never return */
    call kernel_main

    /* Safety net: if kernel_main ever returns, halt the CPU */
    cli                            /* Disable interrupts */
.Lhang:
    hlt                            /* Halt — wakes on NMI, so loop back */
    jmp  .Lhang

.size _start, . - _start
