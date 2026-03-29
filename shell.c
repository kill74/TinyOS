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
#include "net/rtl8139.h"
#include "net/ethernet.h"
#include "net/arp.h"
#include "net/ip.h"
#include "net/tcp.h"
#include "net/socket.h"
#include "fs/fs.h"
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
            case PROC_ZOMBIE:  state = "ZOMBIE"; break;
            case PROC_WAITING: state = "WAITING"; break;
            default: break;
        }
        { const char *s = state; int slen = 0; while (s[slen] != '\0') slen++; for (int i = 0; i < slen; i++) line[len++] = s[i]; }
        if (len < (int)sizeof(line) - 1) line[len++] = '\n';
        line[len] = '\0';
        sys_write(1, line, len);
    }
}

/* Test task for fork - child prints message then exits */
static void fork_child_task(void) {
    int my_pid = sys_getpid();
    char buf[32];
    sys_write(1, "Fork child running, PID=", 24);
    int len = itoa(my_pid, buf, 16);
    sys_write(1, buf, len);
    sys_write(1, "\n", 1);
    sys_write(1, "Child exiting with status 42\n", 28);
    sys_exit(42);
}

/* Test task that does fork and wait */
static void fork_parent_task(void) {
    int parent_pid = sys_getpid();
    char buf[32];
    sys_write(1, "Fork parent starting, PID=", 27);
    int len = itoa(parent_pid, buf, 16);
    sys_write(1, buf, len);
    sys_write(1, "\n", 1);
    
    int child_pid = sys_fork();
    if (child_pid == 0) {
        fork_child_task();
        sys_exit(0);
    } else if (child_pid > 0) {
        sys_write(1, "Parent: forked child PID=", 26);
        len = itoa(child_pid, buf, 16);
        sys_write(1, buf, len);
        sys_write(1, "\n", 1);
        
        int status;
        int waited = sys_wait(&status);
        sys_write(1, "Parent: child ", 14);
        len = itoa(waited, buf, 16);
        sys_write(1, buf, len);
        sys_write(1, " exited with status ", 20);
        len = itoa(status, buf, 16);
        sys_write(1, buf, len);
        sys_write(1, "\n", 1);
    } else {
        sys_write(1, "Fork failed!\n", 13);
    }
    
    sys_write(1, "Fork test complete\n", 19);
    sys_exit(0);
}

/* Test task for sbrk (user heap) */
static void sbrk_test_task(void) {
    sys_write(1, "Testing sbrk...\n", 17);
    
    int old_break = sys_sbrk(0);
    char buf[32];
    int len = itoa(old_break, buf, 16);
    sys_write(1, "Initial break: ", 15);
    sys_write(1, buf, len);
    sys_write(1, "\n", 1);
    
    int new_break = sys_sbrk(1024);
    len = itoa(new_break, buf, 16);
    sys_write(1, "After sbrk(1024): ", 18);
    sys_write(1, buf, len);
    sys_write(1, "\n", 1);
    
    new_break = sys_sbrk(4096);
    len = itoa(new_break, buf, 16);
    sys_write(1, "After sbrk(4096): ", 18);
    sys_write(1, buf, len);
    sys_write(1, "\n", 1);
    
    sys_write(1, "Sbrk test complete\n", 19);
    sys_exit(0);
}

/* Filesystem listing callback */
static int shell_ls_cb(const fs_stat_t *st, void *ctx) {
    (void)ctx;
    const char *type = (st->type == FS_TYPE_DIR) ? "DIR " : "FILE";
    vga_printf("  %s  %6u  %s\n", type, st->size, st->name);
    return 0;
}

/* Shell task: very small command interpreter */
void shell_task(void)
{
    (void)process1_task; // avoid unused; they are linked for run commands
    (void)process2_task;
    const char *welcome = "\n\
  ____                  _             _   _              \n\
 |  _ \\ ___  ___ ___ __| |_ __ _  __ | |_(_) ___  _ __  \n\
 | |_) / _ \\/ __/ __/ _` | '__| |/ / | __| |/ _ \\| '_ \\ \n\
 |  _ < (_) \\__ \\__ (_| | |  |   <  | |_| | (_) | | | |)\n\
 |_| \\_\\___/|___/___\\__,_|_|  |_|\\_\\  \\__|_|\\___/|_| |_|\n\
\n\
Welcome to TinyOS Shell. Type 'help' for commands.\n";
    sys_write(1, welcome, str_len(welcome));
    while (1) {
        /* Set prompt color: light cyan on black */
        vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
        const char *prompt = "> ";
        sys_write(1, prompt, str_len(prompt));
        /* Reset to default colour (light grey on black) */
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
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
            const char *h = "\n\
Available commands:\n\
  help    - Show this help\n\
  echo <text> - Print text\n\
  start1  - Start test task 1\n\
  start2  - Start test task 2\n\
  fork    - Test fork() system call\n\
  sbrk    - Test sbrk() user heap\n\
  ps      - List processes\n\
  clear   - Clear screen\n\
  log <level> - Set log level (none|error|warn|info|debug)\n\
  kill <pid> - Kill process by PID\n\
  ticks   - Show timer ticks\n\
  net     - Show NIC status\n\
  arp     - Show ARP table\n\
  tcp     - Show TCP connections\n\
  listen <port> - Listen on a TCP port\n\
  connect <ip> <port> - Connect to IP:port\n\
  ls      - List files\n\
  touch <name> - Create a file\n\
  rm <name> - Delete a file\n\
  cat <name> - Read file contents\n\
  write <name> <text> - Write to file\n\
  fsinfo  - Show filesystem stats\n\
  exit    - Exit shell (yields)\n\
\n";
            sys_write(1, h, str_len(h));
        } else if (strn_cmp(line, "echo ", 5) == 0) {
            sys_write(1, line + 5, str_len(line + 5));
            sys_write(1, "\n", 1);
        } else if (str_eq(line, "kmem")) {
            kmalloc_stats();
        } else if (str_eq(line, "kmem_check")) {
            kmalloc_check();
        } else if (str_eq(line, "start1")) {
            proc_create(process1_task, -1, PROC_MODE_KERNEL);
        } else if (str_eq(line, "start2")) {
            proc_create(process2_task, -1, PROC_MODE_KERNEL);
        } else if (str_eq(line, "fork")) {
            proc_create(fork_parent_task, -1, PROC_MODE_KERNEL);
        } else if (str_eq(line, "sbrk")) {
            proc_create(sbrk_test_task, -1, PROC_MODE_KERNEL);
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
        } else if (str_eq(line, "net")) {
            rtl8139_dump_status();
        } else if (str_eq(line, "arp")) {
            vga_puts("  ARP Table: (entries learned on incoming packets)\n");
            /* ARP entries are printed on receipt by the ARP module */
        } else if (str_eq(line, "tcp")) {
            vga_puts("  TCP Connections:\n");
            extern tcb_t tcbs[];
            for (int t = 0; t < TCP_MAX_CONN; t++) {
                if (!tcbs[t].in_use) continue;
                static const char *st_names[] = {
                    "CLOSED","LISTEN","SYN_SENT","SYN_RCVD",
                    "ESTABLISHED","FIN_WAIT_1","FIN_WAIT_2",
                    "CLOSE_WAIT","CLOSING","LAST_ACK","TIME_WAIT"
                };
                vga_printf("  [%d] %u.%u.%u.%u:%u <-> %u.%u.%u.%u:%u  %s\n",
                    t,
                    (tcbs[t].local_ip >> 24) & 0xFF,
                    (tcbs[t].local_ip >> 16) & 0xFF,
                    (tcbs[t].local_ip >> 8) & 0xFF,
                    tcbs[t].local_ip & 0xFF,
                    tcbs[t].local_port,
                    (tcbs[t].remote_ip >> 24) & 0xFF,
                    (tcbs[t].remote_ip >> 16) & 0xFF,
                    (tcbs[t].remote_ip >> 8) & 0xFF,
                    tcbs[t].remote_ip & 0xFF,
                    tcbs[t].remote_port,
                    st_names[tcbs[t].state]);
            }
        } else if (strn_cmp(line, "listen ", 7) == 0) {
            int port = parse_int(line + 7);
            int fd = socket_create(SOCK_STREAM);
            if (fd >= 0) {
                sockaddr_in_t addr;
                addr.sin_family = AF_INET;
                addr.sin_port = (uint16_t)port;
                addr.sin_addr = INADDR_ANY;
                if (socket_bind(fd, &addr) == 0 && socket_listen(fd, 4) == 0) {
                    vga_printf("  Listening on port %d (fd %d)\n", port, fd);
                } else {
                    vga_puts("  Listen failed\n");
                    socket_close(fd);
                }
            } else {
                vga_puts("  Socket create failed\n");
            }
        } else if (strn_cmp(line, "connect ", 8) == 0) {
            /* Parse "connect <ip> <port>" */
            const char *args = line + 8;
            /* Find space between IP and port */
            const char *sp = args;
            while (*sp && *sp != ' ') sp++;
            if (*sp == ' ') {
                /* Parse IP — simple dotted decimal */
                uint8_t ip[4] = {0, 0, 0, 0};
                int octet = 0, val = 0;
                for (const char *p = args; p < sp && octet < 4; p++) {
                    if (*p == '.') { ip[octet++] = (uint8_t)val; val = 0; }
                    else if (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); }
                }
                if (octet < 4) ip[octet] = (uint8_t)val;
                int port = parse_int(sp + 1);

                uint32_t target = ip_from_bytes(ip[0], ip[1], ip[2], ip[3]);
                vga_printf("  Connecting to %u.%u.%u.%u:%u...\n",
                           ip[0], ip[1], ip[2], ip[3], port);

                int fd = socket_create(SOCK_STREAM);
                if (fd >= 0) {
                    sockaddr_in_t addr;
                    addr.sin_family = AF_INET;
                    addr.sin_port = (uint16_t)port;
                    addr.sin_addr = target;
                    if (socket_connect(fd, &addr) == 0) {
                        vga_printf("  SYN sent (fd %d). Use 'tcp' to check.\n", fd);
                    } else {
                        vga_puts("  Connect failed\n");
                        socket_close(fd);
                    }
                }
            }
        } else if (str_eq(line, "ls")) {
            vga_puts("  Type  Size    Name\n");
            fs_ls(shell_ls_cb, NULL);
        } else if (strn_cmp(line, "touch ", 6) == 0) {
            int r = fs_create(line + 6, FS_TYPE_FILE);
            if (r == FS_OK) vga_printf("  Created '%s'\n", line + 6);
            else if (r == FS_ERR_EXIST) vga_puts("  File already exists\n");
            else vga_printf("  Error: %d\n", r);
        } else if (strn_cmp(line, "rm ", 3) == 0) {
            int r = fs_unlink(line + 3);
            if (r == FS_OK) vga_printf("  Deleted '%s'\n", line + 3);
            else if (r == FS_ERR_NOENT) vga_puts("  File not found\n");
            else vga_printf("  Error: %d\n", r);
        } else if (strn_cmp(line, "cat ", 4) == 0) {
            int fd = fs_open(line + 4, 0);
            if (fd >= 0) {
                char buf[257];
                int n;
                while ((n = fs_read(fd, buf, 256)) > 0) {
                    buf[n] = '\0';
                    sys_write(1, buf, n);
                }
                sys_write(1, "\n", 1);
                fs_close(fd);
            } else {
                vga_puts("  File not found\n");
            }
        } else if (strn_cmp(line, "write ", 6) == 0) {
            /* Parse "write <name> <text>" */
            const char *rest = line + 6;
            const char *sp = rest;
            while (*sp && *sp != ' ') sp++;
            if (*sp == ' ') {
                /* Extract filename */
                char fname[FS_MAX_NAME];
                int i;
                for (i = 0; i < FS_MAX_NAME - 1 && rest + i < sp; i++)
                    fname[i] = rest[i];
                fname[i] = '\0';

                const char *text = sp + 1;
                int tlen = str_len(text);

                /* Create if doesn't exist */
                int ino = fs_open(fname, 2);
                if (ino < 0) {
                    fs_create(fname, FS_TYPE_FILE);
                    ino = fs_open(fname, 2);
                }
                if (ino >= 0) {
                    /* Seek to end for append */
                    fs_seek(ino, 0, FS_SEEK_END);
                    fs_write(ino, text, tlen);
                    fs_write(ino, "\n", 1);
                    fs_close(ino);
                    vga_printf("  Wrote %d bytes to '%s'\n", tlen + 1, fname);
                } else {
                    vga_puts("  Failed to open file\n");
                }
            }
        } else if (str_eq(line, "fsinfo")) {
            fs_stats();
        } else if (str_eq(line, "exit")) {
            /* Let the shell try to exit by terminating itself */
            proc_sleep_ticks(1); /* yield briefly */
        } else {
            const char *err = "Unknown command. Type 'help' for options.\n";
            sys_write(1, err, str_len(err));
        }
    }
}
