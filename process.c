/* process.c — Process management implementation
 *
 * Supports kernel-mode and user-mode (ring-3) processes. User processes
 * are entered via a crafted IRET frame; the TSS ensures interrupts from
 * ring 3 switch to the process's kernel stack.
 *
 * Preemptive scheduling: the timer IRQ calls schedule() so CPU-bound
 * processes are automatically time-sliced.
 */

#include "process.h"
#include "paging.h"
#include "log.h"
#include "kmalloc.h"
#include "timer.h"
#include <stdint.h>
#include <stddef.h>

extern uint32_t page_directory[1024];
extern void userprog_run(void);

/* ── Global process table ──────────────────────────────────────────────── */
pcb_t proc_table[MAX_PROCESSES];
pcb_t *current_proc = NULL;
int next_pid = 1;

/* ── Preemption control ────────────────────────────────────────────────── */
static int preempt_count = 0;
static int reschedule_pending = 0;
static int total_runnable = 0;

/* ── Forward declarations ──────────────────────────────────────────────── */
extern void context_switch(uint32_t *from_esp, uint32_t *to_esp);
extern void enter_usermode(uint32_t entry, uint32_t user_esp);

/* ── Preemption API ────────────────────────────────────────────────────── */
void preempt_disable(void)
{
    preempt_count++;
}

void preempt_enable(void)
{
    if (preempt_count > 0)
        preempt_count--;
    if (preempt_count == 0 && reschedule_pending) {
        reschedule_pending = 0;
        schedule();
    }
}

/* ── Initialise process management ─────────────────────────────────────── */
void init_processing(void)
{
    LOG_INFO("Initializing process management");

    for (int i = 0; i < MAX_PROCESSES; i++) {
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

/* ── Create a new process ──────────────────────────────────────────────── */
int proc_create(void (*entry_point)(void), int pid, int mode)
{
    LOG_INFO("Creating process entry=0x%x mode=%s",
             (uint32_t)entry_point,
             mode == PROC_MODE_USER ? "user" : "kernel");

    /* Find free slot */
    int slot = -1;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (proc_table[i].state == PROC_UNUSED) {
            slot = i;
            break;
        }
    }
    if (slot == -1) {
        LOG_WARN("No free process slots");
        return -1;
    }

    pcb_t *proc = &proc_table[slot];

    /* PID */
    proc->pid = (pid >= 0) ? pid : next_pid++;
    proc->parent_pid = -1;
    proc->mode = (uint8_t)mode;

    /* Kernel stack */
    proc->kstack_top = (uint32_t)&proc->kstack[KSTACK_SIZE];

    /* User-mode process: allocate user stack + set up page directory */
    if (mode == PROC_MODE_USER) {
        /* Allocate user stack */
        proc->usr_stack_base = (uint32_t)kmalloc(USR_STACK_SIZE);
        if (!proc->usr_stack_base) {
            LOG_ERROR("Failed to allocate user stack for PID %d", proc->pid);
            proc->state = PROC_UNUSED;
            return -1;
        }
        proc->usr_stack_top = proc->usr_stack_base + USR_STACK_SIZE;

        /* Per-process page directory — identity-map first 4 MB as user */
        proc->page_dir = (uint32_t *)kmalloc(1024 * sizeof(uint32_t));
        if (!proc->page_dir) {
            kfree((void *)proc->usr_stack_base);
            proc->state = PROC_UNUSED;
            return -1;
        }

        extern uint32_t first_page_table[];
        for (int i = 0; i < 1024; i++)
            proc->page_dir[i] = 0x00000002;

        /* Copy kernel page tables (>= KERNEL_VIRTUAL_BASE) — supervisor */
        for (int i = 0; i < 1024; i++) {
            uint32_t vaddr = (uint32_t)i * 0x400000;
            if (vaddr >= KERNEL_VIRTUAL_BASE)
                proc->page_dir[i] = page_directory[i] & ~0x4;
        }

        /* Map first 4 MB as user-accessible (covers code + kernel heap) */
        for (int i = 0; i < (4 * 1024 * 1024) / 0x400000; i++)
            proc->page_dir[i] = (i * 0x400000) | 0x7;

        /* VGA buffer accessible from user mode */
        proc->page_dir[0] = ((uint32_t)&first_page_table) | 0x7;
        first_page_table[0xB8] = 0x000B8000 | 0x7;
    } else {
        proc->page_dir = page_directory;
        proc->usr_stack_top = 0;
        proc->usr_stack_base = 0;
    }

    proc->eip = (uint32_t)entry_point;
    proc->heap_end = USER_HEAP_START;

    /* ── Build IRET frame on kernel stack ──────────────────────────────── */
    uint32_t *sp = (uint32_t *)proc->kstack_top;

    if (mode == PROC_MODE_USER) {
        /* IRET to ring 3 */
        *--sp = 0x0000002B;                    /* SS  — user data, ring 3 */
        *--sp = proc->usr_stack_top;            /* ESP — user stack */
        *--sp = 0x00000200;                     /* EFLAGS — IF=1 */
        *--sp = 0x0000001B;                     /* CS  — user code, ring 3 */
        *--sp = (uint32_t)entry_point;          /* EIP — entry point */
    } else {
        /* IRET to ring 0 */
        *--sp = 0x00000010;                     /* SS  — kernel data */
        *--sp = (uint32_t)&proc->kstack[KSTACK_SIZE]; /* ESP */
        *--sp = 0x00000200;                     /* EFLAGS — IF=1 */
        *--sp = 0x00000008;                     /* CS  — kernel code */
        *--sp = (uint32_t)entry_point;          /* EIP */
    }

    /* Error code + vector number (filled by IRQ stubs) */
    *--sp = 0x00000000;                         /* error code */
    *--sp = 0x00000000;                         /* vector number */

    /* General purpose registers */
    *--sp = 0; /* EDI */
    *--sp = 0; /* ESI */
    *--sp = 0; /* EBP */
    *--sp = 0; /* EBX */
    *--sp = 0; /* EDX */
    *--sp = 0; /* ECX */
    *--sp = 0; /* EAX */

    /* Segment registers */
    *--sp = (mode == PROC_MODE_USER) ? 0x23 : 0x10; /* DS */
    *--sp = (mode == PROC_MODE_USER) ? 0x23 : 0x10; /* ES */
    *--sp = (mode == PROC_MODE_USER) ? 0x23 : 0x10; /* FS */
    *--sp = (mode == PROC_MODE_USER) ? 0x23 : 0x10; /* GS */

    proc->kstack_top = (uint32_t)sp;
    proc->state = PROC_READY;
    proc->wake_tick = 0;
    total_runnable++;

    LOG_INFO("Process %d created (mode=%s, state=READY)",
             proc->pid, mode == PROC_MODE_USER ? "user" : "kernel");
    return proc->pid;
}

/* ── Kill a process ────────────────────────────────────────────────────── */
int proc_kill(int pid)
{
    for (int i = 0; i < MAX_PROCESSES; i++) {
        pcb_t *p = &proc_table[i];
        if (p->state != PROC_UNUSED && p->pid == pid) {
            if (p->state == PROC_READY || p->state == PROC_RUNNING)
                total_runnable--;
            p->state = PROC_UNUSED;
            LOG_INFO("Process %d killed", pid);
            return 0;
        }
    }
    return -1;
}

/* ── Scheduler — round-robin with preemptive time-slicing ─────────────── */
void schedule(void)
{
    int start_idx = 0;
    if (current_proc != NULL)
        start_idx = (int)(current_proc - proc_table + 1) % MAX_PROCESSES;

    pcb_t *next_proc = NULL;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        int idx = (start_idx + i) % MAX_PROCESSES;
        if (proc_table[idx].state == PROC_READY ||
            proc_table[idx].state == PROC_RUNNING) {
            next_proc = &proc_table[idx];
            break;
        }
    }

    if (next_proc == NULL) {
        if (current_proc != NULL && current_proc->state == PROC_RUNNING)
            next_proc = current_proc;
        else
            return;
    }

    if (next_proc == current_proc)
        return;

    LOG_DEBUG("Schedule: PID %d -> PID %d",
              current_proc ? current_proc->pid : -1,
              next_proc->pid);

    pcb_t *prev_proc = current_proc;

    /* Voluntary yields already set state to READY; preempted processes
     * stay RUNNING and are set back to READY here so the scheduler can
     * find them again. */
    if (prev_proc != NULL && prev_proc->state == PROC_RUNNING)
        prev_proc->state = PROC_READY;

    current_proc = next_proc;
    current_proc->state = PROC_RUNNING;

    /* Switch page directory */
    if (current_proc->page_dir != NULL) {
        extern void load_page_directory(uint32_t *);
        load_page_directory(current_proc->page_dir);
    }

    /* Context switch (skip if no previous process — first call) */
    if (prev_proc != NULL) {
        context_switch(
            (uint32_t *)&prev_proc->kstack_top,
            (uint32_t *)&current_proc->kstack_top);
    }
}

/* ── Preemption entry point (called from timer IRQ) ───────────────────── */
void preempt(void)
{
    if (!current_proc || total_runnable < 2)
        return;

    if (preempt_count > 0) {
        reschedule_pending = 1;
        return;
    }

    schedule();
}

/* ── Yield voluntarily ────────────────────────────────────────────────── */
void yield(void)
{
    if (current_proc == NULL)
        return;
    LOG_DEBUG("Process %d yielding", current_proc->pid);
    current_proc->state = PROC_READY;
    schedule();
}

/* ── Sleep for N timer ticks ──────────────────────────────────────────── */
void proc_sleep_ticks(uint32_t ticks)
{
    if (current_proc == NULL)
        return;

    if (ticks == 0) {
        yield();
        return;
    }

    current_proc->wake_tick = timer_get_ticks() + ticks;
    current_proc->state = PROC_SLEEPING;
    schedule();
}

/* ── Called from timer IRQ to wake sleeping processes ─────────────────── */
void proc_tick(uint32_t current_tick)
{
    for (int i = 0; i < MAX_PROCESSES; i++) {
        pcb_t *proc = &proc_table[i];
        if (proc->state == PROC_SLEEPING &&
            current_tick >= proc->wake_tick) {
            proc->wake_tick = 0;
            proc->state = PROC_READY;
        }
    }
}

/* ── Fork ─────────────────────────────────────────────────────────────── */
extern uint32_t saved_ebp;

int proc_fork(void)
{
    if (current_proc == NULL)
        return -1;

    int slot = -1;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (proc_table[i].state == PROC_UNUSED) {
            slot = i;
            break;
        }
    }
    if (slot == -1) {
        LOG_WARN("No free slots for fork");
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

    /* Copy parent's kernel stack */
    uint32_t *ps = (uint32_t *)parent->kstack_top;
    uint32_t *cs = (uint32_t *)child->kstack_top;
    size_t words = KSTACK_SIZE / sizeof(uint32_t)
                   - ((ps - (uint32_t *)parent->kstack) & 0x3F);
    if (words > KSTACK_SIZE / sizeof(uint32_t))
        words = KSTACK_SIZE / sizeof(uint32_t);

    for (size_t i = 0; i < words; i++)
        *--cs = *ps++;

    child->kstack_top = (uint32_t)cs;
    child->ebp = saved_ebp;
    child->eip = parent->eip;
    child->eflags = parent->eflags;
    child->eax = 0; /* child returns 0 */

    total_runnable++;
    LOG_INFO("Fork: parent=%d child=%d", parent->pid, child->pid);
    return child->pid;
}

/* ── Exec ─────────────────────────────────────────────────────────────── */
int proc_exec(void (*entry_point)(void))
{
    if (current_proc == NULL || entry_point == NULL)
        return -1;

    current_proc->eip = (uint32_t)entry_point;
    current_proc->eax = current_proc->ebx = 0;
    current_proc->ecx = current_proc->edx = 0;
    current_proc->esi = current_proc->edi = 0;
    current_proc->ebp = 0;
    current_proc->usr_stack_top = current_proc->usr_stack_base + USR_STACK_SIZE;
    current_proc->heap_end = USER_HEAP_START;

    LOG_INFO("Exec: PID %d entry=0x%x", current_proc->pid,
             (uint32_t)entry_point);
    return 0;
}

/* ── Sbrk ─────────────────────────────────────────────────────────────── */
int proc_sbrk(int32_t increment)
{
    if (current_proc == NULL)
        return -1;

    uint32_t old = current_proc->heap_end;
    uint32_t nw = old + increment;

    if (nw < USER_HEAP_START || nw > USER_HEAP_END)
        return -1;

    current_proc->heap_end = nw;
    return (int)old;
}

/* ── Wait ─────────────────────────────────────────────────────────────── */
static pcb_t *find_zombie_child(int parent_pid)
{
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (proc_table[i].state == PROC_ZOMBIE &&
            proc_table[i].parent_pid == parent_pid)
            return &proc_table[i];
    }
    return NULL;
}

int proc_wait(int *status_ptr)
{
    if (current_proc == NULL)
        return -1;

    while (1) {
        pcb_t *z = find_zombie_child(current_proc->pid);
        if (z) {
            int pid = z->pid;
            int status = z->exit_status;
            z->state = PROC_UNUSED;
            z->pid = -1;
            total_runnable--;
            if (status_ptr)
                *status_ptr = status;
            LOG_INFO("Wait: reaped child %d status=%d", pid, status);
            return pid;
        }

        int has_children = 0;
        for (int i = 0; i < MAX_PROCESSES; i++) {
            if (proc_table[i].parent_pid == current_proc->pid &&
                proc_table[i].state != PROC_UNUSED &&
                proc_table[i].state != PROC_ZOMBIE) {
                current_proc->state = PROC_WAITING;
                has_children = 1;
                schedule();
                break;
            }
        }
        if (!has_children)
            break;
    }
    return -1;
}
