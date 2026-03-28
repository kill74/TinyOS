/* syscall.h — System call interface */
#pragma once
#include <stdint.h>
#include "idt.h"

/* System call numbers */
#define SYS_WRITE 1
#define SYS_EXIT 2
#define SYS_GETPID 3
#define SYS_YIELD 4
#define SYS_SLEEP 5
#define SYS_SETLOG 7
#define SYS_READ 6
#define SYS_FORK 8
#define SYS_EXEC 9
#define SYS_SBRK 10
#define SYS_WAIT 11

/* Syscall handler prototype */
void syscall_handler(registers_t *regs);

/* Wrapper functions for user programs */
static inline int32_t sys_write(int32_t fd, const void *buf, uint32_t count)
{
    int32_t ret;
    __asm__ __volatile__(
        "int $0x80"
        : "=a"(ret)
        : "0"(SYS_WRITE), "b"(fd), "c"(buf), "d"(count)
        : "memory");
    return ret;
}

static inline int32_t sys_exit(int32_t status)
{
    int32_t ret;
    __asm__ __volatile__(
        "int $0x80"
        : "=a"(ret)
        : "0"(SYS_EXIT), "b"(status)
        : "memory");
    return ret;
}

static inline int32_t sys_getpid(void)
{
    int32_t ret;
    __asm__ __volatile__(
        "int $0x80"
        : "=a"(ret)
        : "0"(SYS_GETPID)
        : "memory");
    return ret;
}

static inline int32_t sys_yield(void)
{
    int32_t ret;
    __asm__ __volatile__(
        "int $0x80"
        : "=a"(ret)
        : "0"(SYS_YIELD)
        : "memory");
    return ret;
}

static inline int32_t sys_sleep(uint32_t ticks)
{
    int32_t ret;
    __asm__ __volatile__(
        "int $0x80"
        : "=a"(ret)
        : "0"(SYS_SLEEP), "b"(ticks)
        : "memory");
    return ret;
}

/* Set kernel log level (development tool) */
static inline int32_t sys_setlog(int32_t level)
{
    int32_t ret;
    __asm__ __volatile__(
        "int $0x80"
        : "=a"(ret)
        : "0"(SYS_SETLOG), "b"(level)
        : "memory");
    return ret;
}

static inline int32_t sys_fork(void)
{
    int32_t ret;
    __asm__ __volatile__(
        "int $0x80"
        : "=a"(ret)
        : "0"(SYS_FORK)
        : "memory");
    return ret;
}

static inline int32_t sys_exec(void (*entry_point)(void))
{
    int32_t ret;
    __asm__ __volatile__(
        "int $0x80"
        : "=a"(ret)
        : "0"(SYS_EXEC), "b"(entry_point)
        : "memory");
    return ret;
}

static inline int32_t sys_sbrk(int32_t increment)
{
    int32_t ret;
    __asm__ __volatile__(
        "int $0x80"
        : "=a"(ret)
        : "0"(SYS_SBRK), "b"(increment)
        : "memory");
    return ret;
}

static inline int32_t sys_wait(int32_t *status_ptr)
{
    int32_t ret;
    __asm__ __volatile__(
        "int $0x80"
        : "=a"(ret)
        : "0"(SYS_WAIT), "b"(status_ptr)
        : "memory");
    return ret;
}
