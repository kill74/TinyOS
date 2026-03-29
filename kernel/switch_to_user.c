/* switch_to_user.c — Real ring-3 transition
 *
 * enter_usermode() in process_asm.S does the actual IRET to ring 3.
 * This file provides a convenience wrapper that loads the process's
 * page directory and calls the assembly stub.
 *
 * For user-mode processes created via proc_create(..., PROC_MODE_USER)
 * the IRET frame is already set up on the kernel stack, so this
 * standalone function is mainly useful for one-shot transitions.
 */

#include "../include/process.h"
#include "../include/paging.h"
#include "../include/log.h"
#include <stdint.h>

extern void enter_usermode(uint32_t entry, uint32_t user_esp);

void switch_to_user(void)
{
    if (!current_proc || current_proc->mode != PROC_MODE_USER) {
        LOG_WARN("switch_to_user: no user process");
        return;
    }

    LOG_INFO("Switching PID %d to ring 3 at 0x%x (stack 0x%x)",
             current_proc->pid,
             current_proc->eip,
             current_proc->usr_stack_top);

    if (current_proc->page_dir)
        load_page_directory(current_proc->page_dir);

    enter_usermode(current_proc->eip, current_proc->usr_stack_top);

    /* Not reached — execution continues in user mode */
}
