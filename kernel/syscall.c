/* syscall.c — System call implementation */

#include "../include/syscall.h"
#include "../include/log.h"
#include "../include/process.h"
#include "../include/vga.h"
#include <stdint.h>
#include <stddef.h>

/* Forward declarations for implementation functions */
static int32_t sys_write_impl(int32_t fd, const void *buf, uint32_t count);
static int32_t sys_exit_impl(int32_t status);
static int32_t sys_getpid_impl(void);
static void sys_yield_impl(void);
static int32_t sys_sleep_impl(uint32_t ticks);

/* Syscall handler */
void syscall_handler(registers_t *regs)
{
    /* System call number is in eax */
    int32_t syscall_num = regs->eax;
    int32_t ret = 0;

    switch (syscall_num)
    {
    case SYS_WRITE:
        /* sys_write(int fd, const void *buf, uint32_t count) */
        /* Parameters: ebx=fd, ecx=buf, edx=count */
        ret = sys_write_impl(regs->ebx, (const void *)regs->ecx, regs->edx);
        break;

    case SYS_EXIT:
        /* sys_exit(int status) */
        /* Parameter: ebx=status */
        sys_exit_impl(regs->ebx);
        /* Should not return */
        break;

    case SYS_GETPID:
        /* sys_getpid() */
        ret = sys_getpid_impl();
        break;

    case SYS_YIELD:
        /* sys_yield() */
        sys_yield_impl();
        ret = 0;
        break;

    case SYS_SETLOG:
        /* Set kernel log level from user space */
        log_set_level((log_level_t)regs->ebx);
        ret = 0;
        break;

    case SYS_SLEEP:
        /* sys_sleep(uint32_t ticks) */
        /* Parameter: ebx=ticks */
        ret = sys_sleep_impl((uint32_t)regs->ebx);
        break;

    default:
        /* Unknown syscall */
        LOG_ERROR("Unknown syscall: %d", syscall_num);
        ret = -1;
        break;
    }

    /* Return value in eax */
    regs->eax = ret;
}

/* Actual implementations (not wrappers - these are called by the wrappers above) */
static int32_t sys_write_impl(int32_t fd, const void *buf, uint32_t count)
{
    /* For simplicity, we only support fd=1 (stdout) */
    if (fd != 1)
    {
        return -1;
    }

    /* Use VGA driver to output the string */
    const char *str = (const char *)buf;
    for (uint32_t i = 0; i < count; i++)
    {
        vga_putchar(str[i]);
    }
    return count;
}

static int32_t sys_exit_impl(int32_t status)
{
    LOG_INFO("Process %d exiting with status %d",
             current_proc ? current_proc->pid : -1, status);

    /* Mark process as unused */
    if (current_proc != NULL)
    {
        current_proc->state = PROC_UNUSED;
    }

    /* Schedule next process */
    schedule();

    /* Should not reach here */
    return 0;
}

static int32_t sys_getpid_impl(void)
{
    if (current_proc != NULL)
    {
        return current_proc->pid;
    }
    return -1;
}

static void sys_yield_impl(void)
{
    yield();
}

static int32_t sys_sleep_impl(uint32_t ticks)
{
    proc_sleep_ticks(ticks);
    return 0;
}
