/* process.c — Process management implementation */

#include "process.h"
#include "paging.h"
#include "log.h"
#include "kmalloc.h"
#include "timer.h"
#include <stdint.h>
#include <stddef.h>

extern uint32_t page_directory[1024];
extern void userprog_run(void);

/* Global process table */
pcb_t proc_table[MAX_PROCESSES];
pcb_t *current_proc = NULL;
int next_pid = 1;

/* Forward declaration for assembly context switch */
extern void context_switch(uint32_t *from_esp, uint32_t *to_esp);

/* Initialize process management */
void init_processing(void)
{
    LOG_INFO("Initializing process management");

    /* Initialize process table */
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        proc_table[i].state = PROC_UNUSED;
        proc_table[i].pid = -1;
        proc_table[i].parent_pid = -1;
        proc_table[i].exit_status = 0;
        proc_table[i].wake_tick = 0;
        proc_table[i].mode = PROC_MODE_KERNEL;
        proc_table[i].heap_end = USER_HEAP_START;
    }

    LOG_INFO("Process management initialized");
}

/* Create a new process */
int proc_create(void (*entry_point)(void), int pid)
{
    LOG_INFO("Creating process with entry point at 0x%x", (uint32_t)entry_point);

    /* Find free slot in process table */
    int slot = -1;
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        if (proc_table[i].state == PROC_UNUSED)
        {
            slot = i;
            break;
        }
    }

    if (slot == -1)
    {
        LOG_WARN("No free process slots available");
        return -1;
    }

    pcb_t *proc = &proc_table[slot];

    /* Assign PID */
    if (pid >= 0)
    {
        proc->pid = pid;
    }
    else
    {
        proc->pid = next_pid++;
    }

    /* Set up kernel stack */
    proc->kstack_top = (uint32_t)&proc->kstack[KSTACK_SIZE]; /* Stack grows down */

    /* Set up user stack */
    proc->usr_stack_top = (uint32_t)kmalloc(USR_STACK_SIZE) + USR_STACK_SIZE;
    if (proc->usr_stack_top == USR_STACK_SIZE)
    { /* kmalloc failed */
        LOG_ERROR("Failed to allocate user stack for process %d", proc->pid);
        proc->state = PROC_UNUSED;
        return -1;
    }

    /* Set up per-process page directory */
    extern uint32_t page_directory[1024];
    extern uint32_t first_page_table[];
    proc->page_dir = (uint32_t *)kmalloc(1024 * sizeof(uint32_t));
    if (!proc->page_dir) {
        /* Fallback to kernel identity map if allocation fails */
        proc->page_dir = page_directory;
    } else {
        /* Clear the directory (mark all entries Not Present) */
        for (int i = 0; i < 1024; i++)
            proc->page_dir[i] = 0x00000002;   /* Supervisor | RW | !Present */

        /* Map kernel region (>= KERNEL_VIRTUAL_BASE) as read‑only for user */
        for (int i = 0; i < 1024; i++) {
            uint32_t vaddr = i * 0x400000;
            if (vaddr >= KERNEL_VIRTUAL_BASE) {
                /* Copy kernel identity entry but clear the User bit */
                proc->page_dir[i] = first_page_table[i] & ~0x4;   /* remove User flag */
            }
            /* Low region (< KERNEL_VIRTUAL_BASE) left as Not Present for now */
        }

        /* Map the user region (first 4 MB) as present+writable+user */
        for (int i = 0; i < (4*1024*1024)/0x400000; i++) {
            proc->page_dir[i] = (i * 0x400000) | 0x7;         /* P=1, W=1, U=1 */
        }

        /* Map VGA buffer (0x000B8000) as read‑only user */
        uint32_t vga_dir_idx = 0x000B8000 / 0x400000;        /* = 0 */
        uint32_t vga_tbl_idx = (0x000B8000 % 0x400000) / 0x1000; /* = 0xB8 */
        proc->page_dir[vga_dir_idx] = ((uint32_t)&first_page_table) | 0x3;
        first_page_table[vga_tbl_idx] = 0x000B8000 | 0x3;      /* Present + RW */

        /* Map syscall entry point (0x00000080) as read‑only user */
        uint32_t sys_dir_idx = 0x00000080 / 0x400000;        /* = 0 */
        uint32_t sys_tbl_idx = (0x00000080 % 0x400000) / 0x1000; /* = 0 */
        proc->page_dir[sys_dir_idx] = ((uint32_t)&first_page_table) | 0x3;
        first_page_table[sys_tbl_idx] = 0x00000080 | 0x3;
    }

    /* Use entry point directly — user program image loading is deferred to ELF loader */
    proc->eip = (uint32_t)entry_point;

    /* Mark this process as user if it’s the dedicated userprog_run path (Phase A/B) */
    if (entry_point == userprog_run) {
        proc->mode = PROC_MODE_USER;
    } else {
        proc->mode = PROC_MODE_KERNEL;
    }

    /* Initialize CPU context on kernel stack */
    uint32_t *stack_ptr = (uint32_t *)proc->kstack_top;

    /* Push registers in reverse order of pop (for iret) */
    *--stack_ptr = 0x00000200;            /* EFLAGS: IF=1 (interrupts enabled) */
    *--stack_ptr = 0x00000008;            /* CS: kernel code segment */
    *--stack_ptr = (uint32_t)entry_point; /* EIP: entry point */
    *--stack_ptr = 0x00000000;            /* Error code (0 for interrupts) */
    *--stack_ptr = 0x00000000;            /* Vector number (will be set by interrupt handler) */

    /* General purpose registers */
    *--stack_ptr = 0; /* EDI */
    *--stack_ptr = 0; /* ESI */
    *--stack_ptr = 0; /* EBP */
    *--stack_ptr = 0; /* EBX */
    *--stack_ptr = 0; /* EDX */
    *--stack_ptr = 0; /* ECX */
    *--stack_ptr = 0; /* EAX */

    /* Segment registers */
    *--stack_ptr = 0x00000010; /* DS */
    *--stack_ptr = 0x00000010; /* ES */
    *--stack_ptr = 0x00000010; /* FS */
    *--stack_ptr = 0x00000010; /* GS */

    proc->kstack_top = (uint32_t)stack_ptr;
    proc->state = PROC_READY;
    proc->wake_tick = 0;

    LOG_INFO("Process %d created (state: READY)", proc->pid);
    return proc->pid;
}

/* Kill a process by PID - best-effort cleanup */
int proc_kill(int pid)
{
    for (int i = 0; i < MAX_PROCESSES; i++) {
        pcb_t *p = &proc_table[i];
        if (p->state != PROC_UNUSED && p->pid == pid) {
            p->state = PROC_UNUSED;
            LOG_INFO("Process %d killed", pid);
            return 0;
        }
    }
    return -1;
}

/* Scheduler - simple round-robin */
void schedule(void)
{
    /* Find next runnable process */
    int start_idx = 0;
    if (current_proc != NULL)
    {
        start_idx = (current_proc - proc_table + 1) % MAX_PROCESSES;
    }

    pcb_t *next_proc = NULL;
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        int idx = (start_idx + i) % MAX_PROCESSES;
        if (proc_table[idx].state == PROC_READY ||
            proc_table[idx].state == PROC_RUNNING)
        {
            next_proc = &proc_table[idx];
            break;
        }
    }

    /* If no other process ready, current process continues */
    if (next_proc == NULL)
    {
        if (current_proc != NULL && current_proc->state == PROC_RUNNING)
        {
            next_proc = current_proc;
        }
        else
        {
            return; /* No processes to run */
        }
    }

    /* If switching to same process, do nothing */
    if (next_proc == current_proc)
    {
        return;
    }

    LOG_DEBUG("Switching from process %d to %d",
              current_proc ? current_proc->pid : -1,
              next_proc->pid);

    pcb_t *prev_proc = current_proc;

    /* Update states for switch */
    if (prev_proc != NULL && prev_proc->state == PROC_RUNNING)
    {
        prev_proc->state = PROC_READY;
    }
    current_proc = next_proc;
    current_proc->state = PROC_RUNNING;
    /* Switch to the new process's page directory (simple per-process isolation) */
    extern void load_page_directory(uint32_t *dir);
    if (current_proc != NULL && current_proc->page_dir != NULL)
    {
        load_page_directory(current_proc->page_dir);
    }
    /* Phase C: skeleton hook to switch to user mode when running user processes */
    if (current_proc != NULL && current_proc->mode == PROC_MODE_USER) {
        switch_to_user();
    }

    /* Perform context switch */
    if (prev_proc != NULL)
    {
        context_switch(
            (uint32_t *)&prev_proc->kstack_top,
            (uint32_t *)&current_proc->kstack_top);
    }
}

/* Yield processor voluntarily */
void yield(void)
{
    if (current_proc == NULL)
    {
        return;
    }
    LOG_DEBUG("Process %d yielding", current_proc->pid);
    schedule();
}

/* Put current process to sleep for N timer ticks */
void proc_sleep_ticks(uint32_t ticks)
{
    if (current_proc == NULL)
    {
        return;
    }

    if (ticks == 0)
    {
        yield();
        return;
    }

    uint32_t now = timer_get_ticks();

    current_proc->wake_tick = now + ticks;
    current_proc->state = PROC_SLEEPING;
    schedule();
}

/* Called from timer IRQ path to wake sleeping processes */
void proc_tick(uint32_t current_tick)
{
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        pcb_t *proc = &proc_table[i];
        if (proc->state == PROC_SLEEPING && current_tick >= proc->wake_tick)
        {
            proc->wake_tick = 0;
            proc->state = PROC_READY;
        }
    }
}

/* Phase C: switch_to_user is implemented in kernel/switch_to_user.c; this file
 * contains the scheduling glue and paging switch only. */

/* Forward declaration for fork context switch */
extern void context_switch_fork(uint32_t *from_esp, uint32_t *to_esp, uint32_t *from_ebp);

/* Fork - create a copy of the current process */
int proc_fork(void)
{
    if (current_proc == NULL) {
        return -1;
    }

    int slot = -1;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (proc_table[i].state == PROC_UNUSED) {
            slot = i;
            break;
        }
    }

    if (slot == -1) {
        LOG_WARN("No free process slots for fork");
        return -1;
    }

    pcb_t *parent = current_proc;
    pcb_t *child = &proc_table[slot];

    child->pid = next_pid++;
    child->parent_pid = parent->pid;
    child->mode = parent->mode;
    child->state = PROC_READY;
    child->exit_status = 0;
    child->wake_tick = 0;
    child->heap_end = parent->heap_end;

    child->usr_stack_base = parent->usr_stack_base;
    child->usr_stack_top = parent->usr_stack_top;
    child->page_dir = parent->page_dir;

    child->kstack_top = (uint32_t)&child->kstack[KSTACK_SIZE];

    uint32_t *parent_stack = (uint32_t *)parent->kstack_top;
    uint32_t *child_stack = (uint32_t *)child->kstack_top;

    size_t stack_words = (KSTACK_SIZE / sizeof(uint32_t)) - ((parent_stack - (uint32_t *)parent->kstack) & 0x3F);
    if (stack_words > KSTACK_SIZE / sizeof(uint32_t)) {
        stack_words = KSTACK_SIZE / sizeof(uint32_t);
    }

    for (size_t i = 0; i < stack_words; i++) {
        *--child_stack = *parent_stack++;
    }

    child->kstack_top = (uint32_t)child_stack;

    extern uint32_t saved_ebp;
    child->ebp = saved_ebp;

    child->eip = parent->eip;
    child->eflags = parent->eflags;
    child->eax = 0;

    LOG_INFO("Fork: parent=%d, child=%d", parent->pid, child->pid);
    return child->pid;
}

/* Exec - replace current process with new program */
int proc_exec(void (*entry_point)(void))
{
    if (current_proc == NULL || entry_point == NULL) {
        return -1;
    }

    current_proc->eip = (uint32_t)entry_point;
    current_proc->eax = 0;
    current_proc->ebx = 0;
    current_proc->ecx = 0;
    current_proc->edx = 0;
    current_proc->esi = 0;
    current_proc->edi = 0;
    current_proc->ebp = 0;

    current_proc->usr_stack_top = current_proc->usr_stack_base + USR_STACK_SIZE;
    current_proc->heap_end = USER_HEAP_START;

    LOG_INFO("Exec: process %d executing at 0x%x", current_proc->pid, (uint32_t)entry_point);
    return 0;
}

/* Sbrk - grow/shrink user heap */
int proc_sbrk(int32_t increment)
{
    if (current_proc == NULL) {
        return -1;
    }

    uint32_t old_heap_end = current_proc->heap_end;
    uint32_t new_heap_end = old_heap_end + increment;

    if (new_heap_end < USER_HEAP_START) {
        return -1;
    }
    if (new_heap_end > USER_HEAP_END) {
        return -1;
    }

    current_proc->heap_end = new_heap_end;
    return old_heap_end;
}

/* Find zombie child of a parent process */
static pcb_t* find_zombie_child(int parent_pid)
{
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (proc_table[i].state == PROC_ZOMBIE && 
            proc_table[i].parent_pid == parent_pid) {
            return &proc_table[i];
        }
    }
    return NULL;
}

/* Wait - wait for child to exit */
int proc_wait(int *status_ptr)
{
    if (current_proc == NULL) {
        return -1;
    }

    while (1) {
        pcb_t *zombie = find_zombie_child(current_proc->pid);
        if (zombie != NULL) {
            int pid = zombie->pid;
            int status = zombie->exit_status;
            zombie->state = PROC_UNUSED;
            zombie->pid = -1;
            if (status_ptr != NULL) {
                *status_ptr = status;
            }
            LOG_INFO("Wait: reaped child %d, status=%d", pid, status);
            return pid;
        }

        for (int i = 0; i < MAX_PROCESSES; i++) {
            if (proc_table[i].parent_pid == current_proc->pid &&
                proc_table[i].state != PROC_UNUSED &&
                proc_table[i].state != PROC_ZOMBIE) {
                current_proc->state = PROC_WAITING;
                schedule();
                break;
            }
        }

        if (current_proc->state == PROC_WAITING) {
            continue;
        }

        break;
    }

    return -1;
}
