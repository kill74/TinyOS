/* shell.c — Minimal interactive shell for TinyOS
 * Provides basic commands to manage and inspect processes, and to spawn
 * test tasks. This is a user-space style program running in kernel context
 * via the existing process/scheduler. */

/* Use includes from the top-level include/ directory */
#include "include/process.h"
#include "include/log.h"
#include "include/syscall.h"
#include "include/keyboard.h"
#include "include/vga.h"
#include "include/kmalloc.h"
#include "include/timer.h"
/* Minimal strlen replacement for bare-metal environment */
static int str_len(const char *s) { int len = 0; while (s[len] != '\0') len++; return len; }

/* Simple char* compare (like strcmp) */
static int str_eq(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return (*a == *b);
}

/* Simple strncmp implementation */
static int strn_cmp(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i]) return (int)((unsigned char)a[i] - (unsigned char)b[i]);
        if (b[i] == '\0') return 0;
    }
    return 0;
}

/* Simple integer parser (base 10) */
static int parse_int(const char *s) {
    int sign = 1; int val = 0; while (*s==' ') s++; if (*s=='-'){ sign = -1; s++; }
    while (*s>='0' && *s<='9') { val = val*10 + (*s - '0'); s++; }
    return sign*val;
}

/* Map textual log level to enum */
static int log_level_from_string(const char *s) {
    if (str_eq(s, "none")) return LOG_LEVEL_NONE;
    if (str_eq(s, "error")) return LOG_LEVEL_ERROR;
    if (str_eq(s, "warn")) return LOG_LEVEL_WARN;
    if (str_eq(s, "info")) return LOG_LEVEL_INFO;
    if (str_eq(s, "debug")) return LOG_LEVEL_DEBUG;
    return -1;
}

/* Simple memory copy (no libc) */
static void mem_copy(char *dst, const char *src, int n) {
    for (int i = 0; i < n; i++) dst[i] = src[i];
}

/* Forward declarations of test tasks defined in kernel/kernel.c */
extern void process1_task(void);
extern void process2_task(void);

/* Helper: simple integer to string */
static int itoa(int value, char *str, int maxlen) {
    char buf[16];
    int pos = 0;
    int sign = value < 0 ? -1 : 1;
    unsigned int v = value < 0 ? -value : value;
    if (v == 0) {
        if (pos < maxlen) str[pos++] = '0';
        if (pos < maxlen) str[pos] = '\0';
        return pos;
    }
    while (v > 0 && pos < (int)sizeof(buf)) {
        buf[pos++] = '0' + (v % 10);
        v /= 10;
    }
    int p = 0;
    if (sign < 0 && p < maxlen) {
        str[p++] = '-';
    }
    for (int i = pos - 1; i >= 0 && p < maxlen; i--) {
        str[p++] = buf[i];
    }
    if (p < maxlen) str[p] = '\0';
    return p;
}

/* Simple process listing helper */
static void shell_ps(void) {
    sys_write(1, "Process Table:\n", 15);
    for (int i = 0; i < MAX_PROCESSES; i++) {
        pcb_t *p = &proc_table[i];
        if (p->state == PROC_UNUSED) continue;
        char line[64]; int len = 0;
        char tmp[16]; int tlen = 0;
        // PID
        tlen = itoa(p->pid, tmp, 16);
        for (int i = 0; i < tlen; i++) line[len++] = tmp[i];
        mem_copy(line + len, ":", 1); len += 1;
        // State
        const char *state = "UNKNOWN";
        switch (p->state) {
            case PROC_RUNNING: state = "RUNNING"; break;
            case PROC_READY:   state = "READY"; break;
            case PROC_BLOCKED: state = "BLOCKED"; break;
            case PROC_SLEEPING:state = "SLEEPING"; break;
            default: break;
        }
        { const char *s = state; int slen = 0; while (s[slen] != '\0') slen++; for (int i = 0; i < slen; i++) line[len++] = s[i]; }
        if (len < (int)sizeof(line) - 1) line[len++] = '\n';
        line[len] = '\0';
        sys_write(1, line, len);
    }
}

/* Shell task: very small command interpreter */
void shell_task(void)
{
    (void)process1_task; // avoid unused; they are linked for run commands
    (void)process2_task;
    const char *welcome = "TinyOS Shell. Type 'help' for commands.\n";
    sys_write(1, welcome, str_len(welcome));
    while (1) {
        const char *prompt = "> ";
        sys_write(1, prompt, 2);
        char line[128]; int idx = 0;
        while (idx < (int)sizeof(line) - 1) {
            if (keyboard_has_char()) {
                char ch = keyboard_read_char();
                /* newline or carriage return ends the line */
                if (ch == '\n' || ch == '\r') break;
                /* Backspace handling */
                if (ch == '\b' || ch == 0x7f) {
                    if (idx > 0) {
                        idx--;
                        sys_write(1, "\b \b", 4);
                    }
                    continue;
                }
                line[idx++] = ch;
                sys_write(1, &ch, 1); /* echo */
            } else {
                /* no input yet, yield to scheduler */
                sys_yield();
            }
        }
        line[idx] = '\0';
        if (str_eq(line, "help")) {
            const char *h = "Commands: help, echo <text>, start1, start2, ps, clear\n";
            sys_write(1, h, str_len(h));
        } else if (strn_cmp(line, "echo ", 5) == 0) {
            sys_write(1, line + 5, str_len(line + 5));
            sys_write(1, "\n", 1);
        } else if (str_eq(line, "kmem")) {
            kmalloc_stats();
        } else if (str_eq(line, "kmem_check")) {
            kmalloc_check();
        } else if (str_eq(line, "start1")) {
            proc_create(process1_task, -1);
        } else if (str_eq(line, "start2")) {
            proc_create(process2_task, -1);
        } else if (str_eq(line, "ps")) {
            shell_ps();
        } else if (str_eq(line, "clear")) {
            vga_clear();
        } else if (strn_cmp(line, "log ", 4) == 0) {
            int lvl = log_level_from_string(line + 4);
            if (lvl >= 0) {
                sys_setlog(lvl);
                const char *ok = "Log level updated\n";
                sys_write(1, ok, str_len(ok));
            } else {
                const char *err = "Unknown log level. Use none|error|warn|info|debug\n";
                sys_write(1, err, str_len(err));
            }
        } else if (strn_cmp(line, "kill ", 5) == 0) {
            int pid = parse_int(line + 5);
            int r = proc_kill(pid);
            if (r == 0) {
                char msg[32]; int l = itoa(pid, msg, 10); sys_write(1, "Killed PID ", 11); sys_write(1, msg, l); sys_write(1, "\n", 1);
            } else {
                sys_write(1, "PID not found\n", 14);
            }
        } else if (str_eq(line, "ticks")) {
            /* Show current timer ticks */
            uint32_t t = timer_get_ticks();
            char num[16]; int len = itoa((int)t, num, 10);
            sys_write(1, "Ticks: ", 7);
            sys_write(1, num, len);
            sys_write(1, "\n", 1);
        } else if (str_eq(line, "exit")) {
            /* Let the shell try to exit by terminating itself */
            proc_sleep_ticks(1); /* yield briefly */
        } else {
            const char *err = "Unknown command. Type 'help' for options.\n";
            sys_write(1, err, str_len(err));
        }
    }
}
