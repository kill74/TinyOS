/* userprog_run.c — Runner for the tiny in-kernel user program blob */

#include "include/userprog.h"
#include "include/vga.h"
#include "include/process.h"
#include "include/timer.h"
#include "include/log.h"
#include <stdint.h>

static void print_dec(uint32_t v)
{
    char buf[12];
    int n = 0;
    if (v == 0) {
        vga_putchar('0');
        return;
    }
    while (v) {
        buf[n++] = '0' + (v % 10);
        v /= 10;
    }
    for (int i = n - 1; i >= 0; --i)
        vga_putchar(buf[i]);
}

static void print_hex(uint32_t v)
{
    char hexbuf[9];
    int n = 0;
    const char *digits = "0123456789abcdef";
    if (v == 0) {
        vga_putchar('0');
        return;
    }
    while (v) {
        hexbuf[n++] = digits[v & 0xF];
        v >>= 4;
    }
    for (int i = n - 1; i >= 0; --i)
        vga_putchar(hexbuf[i]);
}

void userprog_run(void)
{
    const uint8_t *blob = userprog_blob;
    size_t size = userprog_blob_size;
    size_t ip = 0;

    while (ip < size) {
        uint8_t op = blob[ip++];
        switch (op) {
        case 0x01: { /* PRINT_STRING */
            if (ip + 1 >= size) return;
            uint16_t len = blob[ip] | (blob[ip + 1] << 8);
            ip += 2;
            for (int i = 0; i < len && ip < size; i++)
                vga_putchar((char)blob[ip++]);
            break;
        }
        case 0x02: { /* SLEEP */
            if (ip + 1 >= size) return;
            uint16_t t = blob[ip] | (blob[ip + 1] << 8);
            ip += 2;
            proc_sleep_ticks((uint32_t)t);
            break;
        }
        case 0x03: { /* YIELD */
            yield();
            break;
        }
        case 0x04: { /* PRINT_INT */
            if (ip + 3 >= size) return;
            uint32_t v = blob[ip] | (blob[ip + 1] << 8) |
                         (blob[ip + 2] << 16) | (blob[ip + 3] << 24);
            ip += 4;
            print_dec(v);
            break;
        }
        case 0x05: { /* PRINT_HEX */
            if (ip + 3 >= size) return;
            uint32_t v = blob[ip] | (blob[ip + 1] << 8) |
                         (blob[ip + 2] << 16) | (blob[ip + 3] << 24);
            ip += 4;
            print_hex(v);
            break;
        }
        case 0xFF: /* END */
            return;
        default:
            return; /* unknown opcode — halt */
        }
    }
}
