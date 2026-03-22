/* process.h — Process management */
#pragma once
#include <stdint.h>

#define MAX_PROCESSES 4
#define KSTACK_SIZE 4096
#define USR_STACK_SIZE 4096

/* Process states */
typedef enum
{
    PROC_UNUSED = 0,
    PROC_RUNNING,
    PROC_READY,
    PROC_BLOCKED,
    PROC_SLEEPING
} proc_state_t;

/* Process Control Block */
typedef struct
{
    /* CPU state */
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp;
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    uint16_t gs;
    uint16_t fs;
    uint16_t es;
    uint16_t ds;
    uint32_t eflags;
    uint32_t eip;
    uint16_t cs;
    uint16_t ss;

    /* Process info */
    int pid;
    proc_state_t state;
    uint32_t wake_tick;     /* Tick when sleeping process becomes READY */
    uint32_t kstack_top;    /* Kernel stack top */
    uint32_t usr_stack_top; /* User stack top */
    uint32_t *page_dir;     /* Page directory (cr3 value) */

    /* Kernel stack (grows down from high addresses) */
    uint8_t kstack[KSTACK_SIZE];
} pcb_t;

/* Function prototypes */
void init_processing(void);
int proc_create(void (*entry_point)(void), int pid);
void schedule(void);
void switch_to_user(void);
void yield(void);
void proc_sleep_ticks(uint32_t ticks);
void proc_tick(uint32_t current_tick);
extern pcb_t *current_proc;
extern pcb_t proc_table[MAX_PROCESSES];
extern int next_pid;
