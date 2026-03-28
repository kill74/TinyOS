/* stdlib.h — Standard library utilities */
#pragma once
#include <stddef.h>

/* ── String ↔ number conversions ─────────────────────────────────────────── */
int atoi(const char *nptr);
long atol(const char *nptr);

/* ── Number → string conversions ─────────────────────────────────────────── */
char *itoa(int value, char *str, int base);
char *utoa(unsigned int value, char *str, int base);

/* ── Heap allocation (wraps kernel kmalloc/kfree via syscalls) ───────────── */
void *malloc(size_t size);
void  free(void *ptr);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);

/* ── Misc ────────────────────────────────────────────────────────────────── */
void exit(int status) __attribute__((noreturn));
void abort(void) __attribute__((noreturn));
int  abs(int x);
