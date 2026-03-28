/* process_asm.S — Assembly helpers for process management */

.global context_switch
.global saved_ebp

.data
saved_ebp:
    .long 0

.text

/* void context_switch(uint32_t *from_esp, uint32_t *to_esp);
 * Save current stack pointer in *from_esp, load new stack pointer from *to_esp.
 */
context_switch:
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
    ret
