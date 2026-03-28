/* stdlib.c — Standard library utilities */

#include "../libc/stdlib.h"
#include "../libc/string.h"
#include <stdint.h>

/* ── String → number ─────────────────────────────────────────────────────── */

int atoi(const char *nptr)
{
    int result = 0;
    int sign = 1;

    while (*nptr == ' ' || *nptr == '\t') nptr++;
    if (*nptr == '-') { sign = -1; nptr++; }
    else if (*nptr == '+') nptr++;

    while (*nptr >= '0' && *nptr <= '9') {
        result = result * 10 + (*nptr - '0');
        nptr++;
    }
    return result * sign;
}

long atol(const char *nptr)
{
    return (long)atoi(nptr);
}

/* ── Number → string ─────────────────────────────────────────────────────── */

static void reverse(char *str, int len)
{
    for (int i = 0, j = len - 1; i < j; i++, j--) {
        char tmp = str[i];
        str[i] = str[j];
        str[j] = tmp;
    }
}

char *itoa(int value, char *str, int base)
{
    if (base < 2 || base > 36) { str[0] = '\0'; return str; }

    int i = 0;
    int neg = 0;
    unsigned int u;

    if (value < 0 && base == 10) {
        neg = 1;
        u = (unsigned int)(-(value + 1)) + 1u;
    } else {
        u = (unsigned int)value;
    }

    if (u == 0) { str[i++] = '0'; }
    else {
        while (u > 0) {
            int d = u % base;
            str[i++] = (d < 10) ? '0' + d : 'a' + d - 10;
            u /= base;
        }
    }
    if (neg) str[i++] = '-';
    str[i] = '\0';
    reverse(str, i);
    return str;
}

char *utoa(unsigned int value, char *str, int base)
{
    if (base < 2 || base > 36) { str[0] = '\0'; return str; }

    int i = 0;
    if (value == 0) { str[i++] = '0'; }
    else {
        while (value > 0) {
            int d = value % base;
            str[i++] = (d < 10) ? '0' + d : 'a' + d - 10;
            value /= base;
        }
    }
    str[i] = '\0';
    reverse(str, i);
    return str;
}

/* ── Heap allocation ─────────────────────────────────────────────────────── */
/* These are thin wrappers around the kernel's kmalloc/kfree.
 * In a real OS with user/kernel separation, these would use sbrk() or mmap().
 * For TinyOS running in kernel mode, we use kmalloc directly. */

extern void *kmalloc(size_t size);
extern void kfree(void *ptr);

void *malloc(size_t size)
{
    if (size == 0) return NULL;
    return kmalloc(size);
}

void free(void *ptr)
{
    if (ptr) kfree(ptr);
}

void *calloc(size_t nmemb, size_t size)
{
    size_t total = nmemb * size;
    if (nmemb != 0 && total / nmemb != size) return NULL; /* overflow */
    void *p = malloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void *realloc(void *ptr, size_t size)
{
    if (!ptr) return malloc(size);
    if (size == 0) { free(ptr); return NULL; }

    /* Simple realloc: alloc new, copy, free old */
    void *newp = malloc(size);
    if (newp) {
        /* We don't know the old size — copy conservatively */
        memcpy(newp, ptr, size);
        free(ptr);
    }
    return newp;
}

/* ── Misc ────────────────────────────────────────────────────────────────── */

void exit(int status)
{
    (void)status;
    while (1) __asm__ __volatile__("hlt");
}

void abort(void)
{
    exit(-1);
}

int abs(int x)
{
    return x < 0 ? -x : x;
}
