/* process.c — Process management implementation */

#include "process.h"
#include "log.h"
#include "paging.h"
#include "kmalloc.h"
#include "timer.h"
#include <stdint.h>

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
        proc_table[i].wake_tick = 0;
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

    /* Set up page directory (inherit from kernel for now) */
    extern uint32_t page_directory[1024];
    proc->page_dir = page_directory;

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

/* Switch to user mode (simplified) */
void switch_to_user(void)
{
    /* In a full implementation, this would:
       1. Set up user segment registers
       2. Set up user stack
       3. Use iret to return to user mode
     */
    LOG_DEBUG("Switching to user mode for process %d", current_proc->pid);
    /* Actual implementation would be in assembly */
}
