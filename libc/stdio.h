/* stdio.h — Standard I/O (uses syscalls under the hood) */
#pragma once
#include <stddef.h>
#include <stdarg.h>

/* ── File descriptors ────────────────────────────────────────────────────── */
#define STDIN_FILENO   0
#define STDOUT_FILENO  1
#define STDERR_FILENO  2

/* ── Low-level I/O (syscall wrappers) ────────────────────────────────────── */
int write(int fd, const void *buf, size_t count);
int read(int fd, void *buf, size_t count);

/* ── Formatted output ────────────────────────────────────────────────────── */
int printf(const char *fmt, ...);
int sprintf(char *buf, const char *fmt, ...);
int snprintf(char *buf, size_t size, const char *fmt, ...);

/* ── Simple output ───────────────────────────────────────────────────────── */
int puts(const char *s);
int putchar(int c);

/* ── Misc ────────────────────────────────────────────────────────────────── */
void perror(const char *s);
