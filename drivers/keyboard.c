/* keyboard.c — PS/2 keyboard driver (Scancode Set 1)
 *
 * The PS/2 keyboard controller lives at I/O port 0x60 (data) and 0x64 (status).
 * When a key is pressed or released the controller fires IRQ1 and puts a
 * "scancode" byte in port 0x60.
 *
 * Scancode Set 1 (the default after reset):
 *   - Press   → byte < 0x80 (bit 7 clear)
 *   - Release → byte = press_scancode | 0x80
 *
 * We translate scancodes into ASCII using a simple lookup table.
 */

#include "../include/keyboard.h"
#include "../include/irq.h"
#include "../include/vga.h"
#include "../include/log.h"
#include <stdint.h>

#define KB_DATA_PORT 0x60

/* Simple ring buffer for keyboard input so user tasks can read characters */
#define KB_BUFFER_SIZE 64
static volatile char kb_buffer[KB_BUFFER_SIZE];
static volatile uint32_t kb_head = 0; /* write index */
static volatile uint32_t kb_tail = 0; /* read index */

extern uint8_t inb(uint16_t port);
/* irq_enable is declared in irq.h — no local extern needed */

/* ── Scancode → ASCII table ───────────────────────────────────────────────── */
/* Index = scancode (Set 1, unshifted). 0 means "no printable character". */
static const char scancode_ascii[] = {
    0,
    0,
    '1',
    '2',
    '3',
    '4',
    '5',
    '6',
    '7',
    '8', /*  0– 9 */
    '9',
    '0',
    '-',
    '=',
    '\b',
    '\t', /* 10–15 */
    'q',
    'w',
    'e',
    'r',
    't',
    'y',
    'u',
    'i',
    'o',
    'p', /* 16–25 */
    '[',
    ']',
    '\n',
    0, /* 26–29 */
    'a',
    's',
    'd',
    'f',
    'g',
    'h',
    'j',
    'k',
    'l',
    ';', /* 30–39 */
    '\'',
    '`',
    0,
    '\\', /* 40–43 */
    'z',
    'x',
    'c',
    'v',
    'b',
    'n',
    'm',
    ',',
    '.',
    '/', /* 44–53 */
    0,
    '*',
    0,
    ' ', /* 54–57 */
};
#define SCANCODE_TABLE_SIZE 58

/* ── Module state ─────────────────────────────────────────────────────────── */
static volatile char last_char = 0;

/* ── IRQ1 handler ─────────────────────────────────────────────────────────── */
static void keyboard_callback(registers_t *regs)
{
    (void)regs;

    uint8_t scancode = inb(KB_DATA_PORT);

    /* Bit 7 set = key release event — ignore it */
    if (scancode & 0x80)
    {
        return;
    }

    if (scancode < SCANCODE_TABLE_SIZE)
    {
        char c = scancode_ascii[scancode];
        if (c)
        {
            last_char = c;
            vga_putchar(c); /* Echo the character to the screen */
            /* Push into ring buffer if there is space */
            uint32_t next_head = (kb_head + 1) % KB_BUFFER_SIZE;
            if (next_head != kb_tail) {
                kb_buffer[kb_head] = c;
                kb_head = next_head;
            }
        }
    }
}

/* ── Public API ───────────────────────────────────────────────────────────── */
void init_keyboard(void)
{
    LOG_INFO("Initializing keyboard");
    irq_register_handler(1, keyboard_callback);
    irq_enable(1);
    LOG_INFO("Keyboard initialized");
}

char keyboard_last_char(void)
{
    return last_char;
}

/* Keyboard buffer helpers (for user-space/read). */
int keyboard_has_char(void) {
    return kb_head != kb_tail;
}

char keyboard_read_char(void) {
    if (kb_tail == kb_head) {
        return 0; /* no data */
    }
    char c = kb_buffer[kb_tail];
    kb_tail = (kb_tail + 1) % KB_BUFFER_SIZE;
    return c;
}
