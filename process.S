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
 * Switch to user mode. For now, we just return.
 * A full implementation would set up user segments and use iret.
 */
switch_to_user:
    ret