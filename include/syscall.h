/* syscall.h — System call interface */
#pragma once
#include <stdint.h>
#include "idt.h"

/* System call numbers */
#define SYS_WRITE   1
#define SYS_EXIT    2
#define SYS_GETPID  3
#define SYS_YIELD   4

/* Syscall handler prototype */
void syscall_handler(registers_t *regs);

/* Wrapper functions for user programs */
static inline int32_t sys_write(int32_t fd, const void *buf, uint32_t count) {
    int32_t ret;
    __asm__ __volatile__(
        "int $0x80"
        : "=a" (ret)
        : "0" (SYS_WRITE), "b" (fd), "c" (buf), "d" (count)
        : "memory"
    );
    return ret;
}

static inline int32_t sys_exit(int32_t status) {
    int32_t ret;
    __asm__ __volatile__(
        "int $0x80"
        : "=a" (ret)
        : "0" (SYS_EXIT), "b" (status)
        : "memory"
    );
    return ret;
}

static inline int32_t sys_getpid(void) {
    int32_t ret;
    __asm__ __volatile__(
        "int $0x80"
        : "=a" (ret)
        : "0" (SYS_GETPID)
        : "memory"
    );
    return ret;
}

static inline int32_t sys_yield(void) {
    int32_t ret;
    __asm__ __volatile__(
        "int $0x80"
        : "=a" (ret)
        : "0" (SYS_YIELD)
        : "memory"
    );
    return ret;
}