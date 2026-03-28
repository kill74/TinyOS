/* unistd.h — POSIX-like system calls */
#pragma once
#include <stddef.h>

/* ── Process management ──────────────────────────────────────────────────── */
int  fork(void);
int  exec(const char *path);
void exit(int status);
int  wait(int *status);
int  getpid(void);

/* ── I/O ─────────────────────────────────────────────────────────────────── */
int  open(const char *path, int flags);
int  close(int fd);
int  read(int fd, void *buf, size_t count);
int  write(int fd, const void *buf, size_t count);

/* ── Memory ──────────────────────────────────────────────────────────────── */
void *sbrk(int increment);

/* ── Timing ──────────────────────────────────────────────────────────────── */
unsigned int sleep(unsigned int ticks);

/* ── Yield CPU to scheduler ──────────────────────────────────────────────── */
void yield(void);

/* ── Set log level ───────────────────────────────────────────────────────── */
void setlog(int level);
