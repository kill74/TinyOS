/* process.S — Assembly helpers for process management */

.global context_switch
.global yield
.global switch_to_user

/* void context_switch(uint32_t *from_esp, uint32_t *to_esp);
 * Save current stack pointer in *from_esp, load new stack pointer from *to_esp.
 */
context_switch:
    pushl %ebp
    movl %esp, %ebp

    /* Save current stack pointer */
    movl 8(%ebp), %eax     /* from_esp */
    movl %esp, (%eax)

    /* Load new stack pointer */
    movl 12(%ebp), %eax    /* to_esp */
    movl (%eax), %esp

    popl %ebp
    ret

/* void yield(void);
 * Trigger a software interrupt to call the scheduler.
 * We'll use interrupt 0x80 for system calls and yield.
 */
yield:
    int $0x80
    ret

/* void switch_to_user(void);
 * Switch to user mode via iret.
 * We assume we are called from kernel mode (CPL=0).
 * We will build an iret frame on the user stack and then iret to user code.
 */
switch_to_user:
    /* Get current PCB pointer */
    movl _current_proc, %eax

    /* Load user stack top (offset 69) */
    movl 69(%eax), %edx          /* edx = usr_stack_top */

    /* Build interrupt frame on user stack (grows down) */
    /* We'll subtract 20 bytes (5 dwords) for: ss, esp, eflags, cs, eip */
    subl $20, %edx               /* edx points to bottom of frame */

    /* ss = user data selector (0x23) */
    movw $0x23, %cx
    movw %cx, (%edx)             /* [esp] = ss */

    /* esp after iret = original usr_stack_top */
    addl $20, %edx               /* edx = original usr_stack_top */
    movl %edx, 4(%edx)           /* [esp+4] = esp (value to pop into esp) */

    /* eflags = IF=1 (interrupts enabled) */
    movl $0x200, %ecx
    movl %ecx, 8(%edx)           /* [esp+8] = eflags */

    /* cs = user code selector (0x1B) */
    movw $0x1B, %cx
    movw %cx, 12(%edx)           /* [esp+12] = cs */

    /* eip = entry point from PCB (offset 44) */
    movl 44(%eax), %ecx
    movl %ecx, 16(%edx)          /* [esp+16] = eip */

    /* Switch to user stack and execute iret */
    movl %edx, %esp
    iret