/* process_asm.S — Assembly helpers for process management */

.global context_switch
.global enter_usermode
.global saved_ebp

.data
saved_ebp:
    .long 0

.text

/* void context_switch(uint32_t *from_esp, uint32_t *to_esp);
 * Save current stack pointer in *from_esp, load new stack pointer from *to_esp.
 * Interrupts are disabled during the switch to prevent reentrancy.
 */
context_switch:
    pushfl                  /* save EFLAGS (including IF) */
    cli                     /* disable interrupts during switch */

    pushl %ebp
    movl %esp, %ebp
    movl %ebp, saved_ebp

    /* Save current stack pointer */
    movl 8(%ebp), %eax     /* from_esp */
    movl %esp, (%eax)

    /* Load new stack pointer */
    movl 12(%ebp), %eax    /* to_esp */
    movl (%eax), %esp

    popl %ebp

    popfl                   /* restore EFLAGS (re-enables interrupts if they were on) */
    ret

/* void enter_usermode(uint32_t entry, uint32_t user_esp);
 *
 * Switch from ring 0 to ring 3 via a crafted IRET frame.
 * The CPU pops: EIP, CS, EFLAGS, ESP, SS — we push them in reverse.
 *
 * This function does NOT return in the normal sense: execution continues
 * at `entry` in user mode (ring 3). If the user code executes int 0x80
 * or a hardware interrupt fires, the CPU switches back to the kernel
 * stack (via TSS.ESP0) and the interrupt path handles it.
 */
enter_usermode:
    cli

    mov  8(%esp), %ecx     /* ecx = entry point */
    mov  12(%esp), %edx    /* edx = user stack top */

    /* Switch to user data segments */
    mov  $0x23, %ax        /* 0x20 | 3 = user data, ring 3 */
    mov  %ax, %ds
    mov  %ax, %es
    mov  %ax, %fs
    mov  %ax, %gs

    /* Build IRET frame on the current (kernel) stack */
    pushl $0x23            /* SS  — user data segment, ring 3 */
    pushl %edx             /* ESP — user stack pointer */
    pushl $0x200           /* EFLAGS — IF=1 (interrupts enabled) */
    pushl $0x1B            /* CS  — user code segment (0x18 | 3) */
    pushl %ecx             /* EIP — user entry point */

    iret                   /* Pop EIP, CS, EFLAGS, ESP, SS → ring 3 */
