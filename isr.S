/* isr.S — Interrupt Service Routine entry stubs
 *
 * The CPU jumps here when an interrupt or exception fires. These stubs:
 *   1. Push a uniform register frame onto the stack.
 *   2. Call the shared C dispatcher (isr_handler or irq_handler).
 *   3. Restore registers and return with `iret`.
 *
 * Two flavours of stub are needed:
 *   - ISR_NOERRCODE: CPU does NOT push an error code (most exceptions + all IRQs).
 *     We push a dummy 0 so the stack frame is always the same shape.
 *   - ISR_ERRCODE:   CPU already pushed an error code — we just note the int number.
 */

/* ── Macros ───────────────────────────────────────────────────────────────── */

.macro ISR_NOERRCODE  num
.global isr\num
isr\num:
    cli
    push $0             /* dummy error code */
    push $\num          /* interrupt number  */
    jmp  isr_common
.endm

.macro ISR_ERRCODE  num
.global isr\num
isr\num:
    cli
                        /* error code already on stack */
    push $\num
    jmp  isr_common
.endm

.macro IRQ  num  mapped
.global irq\num
irq\num:
    cli
    push $0
    push $\mapped       /* IRQs are remapped to vectors 32–47 */
    jmp  irq_common
.endm

/* ── CPU Exceptions (vectors 0–31) ───────────────────────────────────────── */
ISR_NOERRCODE  0    /* #DE  Divide Error              */
ISR_NOERRCODE  1    /* #DB  Debug                     */
ISR_NOERRCODE  2    /*      Non-Maskable Interrupt    */
ISR_NOERRCODE  3    /* #BP  Breakpoint                */
ISR_NOERRCODE  4    /* #OF  Overflow                  */
ISR_NOERRCODE  5    /* #BR  Bound Range Exceeded      */
ISR_NOERRCODE  6    /* #UD  Invalid Opcode            */
ISR_NOERRCODE  7    /* #NM  Device Not Available      */
ISR_ERRCODE    8    /* #DF  Double Fault              */
ISR_NOERRCODE  9    /*      Coprocessor Segment (old) */
ISR_ERRCODE   10    /* #TS  Invalid TSS               */
ISR_ERRCODE   11    /* #NP  Segment Not Present       */
ISR_ERRCODE   12    /* #SS  Stack Fault               */
ISR_ERRCODE   13    /* #GP  General Protection Fault  */
ISR_ERRCODE   14    /* #PF  Page Fault                */
ISR_NOERRCODE 15    /*      Reserved                  */
ISR_NOERRCODE 16    /* #MF  x87 FPU Error             */
ISR_ERRCODE   17    /* #AC  Alignment Check           */
ISR_NOERRCODE 18    /* #MC  Machine Check             */
ISR_NOERRCODE 19    /* #XM  SIMD Exception            */
ISR_NOERRCODE 20
ISR_NOERRCODE 21
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_NOERRCODE 28
ISR_NOERRCODE 29
ISR_NOERRCODE 30
ISR_NOERRCODE 31

/* ── Syscall trap (vector 0x80, ring-3 accessible) ─────────────────────── */
ISR_NOERRCODE 128

/* ── Hardware IRQs (vectors 32–47, remapped from 0–15) ────────────────────── */
IRQ  0, 32    /* Programmable Interval Timer */
IRQ  1, 33    /* PS/2 Keyboard               */
IRQ  2, 34    /* Cascade (internal PIC link) */
IRQ  3, 35    /* COM2                        */
IRQ  4, 36    /* COM1                        */
IRQ  5, 37    /* LPT2 / Sound card           */
IRQ  6, 38    /* Floppy disk                 */
IRQ  7, 39    /* LPT1 / Spurious             */
IRQ  8, 40    /* CMOS Real-Time Clock        */
IRQ  9, 41    /* Free / ACPI                 */
IRQ 10, 42    /* Free                        */
IRQ 11, 43    /* Free                        */
IRQ 12, 44    /* PS/2 Mouse                  */
IRQ 13, 45    /* FPU / Coprocessor           */
IRQ 14, 46    /* Primary ATA                 */
IRQ 15, 47    /* Secondary ATA               */

/* ── Common ISR path (CPU exceptions) ────────────────────────────────────── */
.extern isr_handler

isr_common:
    pusha                           /* Push EAX,ECX,EDX,EBX,ESP,EBP,ESI,EDI */

    mov  %ds, %ax                   /* Save data segment */
    push %eax

    mov  $0x10, %ax                 /* Load kernel data segment (GDT entry 2) */
    mov  %ax, %ds
    mov  %ax, %es
    mov  %ax, %fs
    mov  %ax, %gs

    call isr_handler                /* C handler receives pointer to stack frame */

    pop  %eax                       /* Restore data segment */
    mov  %ax, %ds
    mov  %ax, %es
    mov  %ax, %fs
    mov  %ax, %gs

    popa
    add  $8, %esp                   /* Clean up pushed error code + interrupt number */
    iret

/* ── Common IRQ path (hardware interrupts) ────────────────────────────────── */
.extern irq_handler

irq_common:
    pusha

    mov  %ds, %ax
    push %eax

    mov  $0x10, %ax
    mov  %ax, %ds
    mov  %ax, %es
    mov  %ax, %fs
    mov  %ax, %gs

    call irq_handler

    pop  %eax
    mov  %ax, %ds
    mov  %ax, %es
    mov  %ax, %fs
    mov  %ax, %gs

    popa
    add  $8, %esp
    iret
