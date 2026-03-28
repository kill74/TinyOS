/* mouse.c — PS/2 mouse driver
 *
 * The PS/2 mouse is controlled through the keyboard controller (8042).
 * Data arrives as 3-byte packets via IRQ12.
 *
 * Packet format:
 *   Byte 0: [Y_ovf X_ovf Y_sign X_sign 1 M R L]
 *   Byte 1: X delta (signed, 9-bit sign-extended)
 *   Byte 2: Y delta (signed, 9-bit sign-extended)
 */

#include "../gui/mouse.h"
#include "../gui/graphics.h"
#include "../include/irq.h"
#include "../include/log.h"
#include <stdint.h>

/* ── I/O helpers ─────────────────────────────────────────────────────────── */
extern void outb(uint16_t port, uint8_t value);
extern uint8_t inb(uint16_t port);
extern void io_wait(void);

/* ── PS/2 controller ports ──────────────────────────────────────────────── */
#define PS2_DATA    0x60
#define PS2_STATUS  0x64
#define PS2_CMD     0x64

/* ── PS/2 controller commands ───────────────────────────────────────────── */
#define PS2_CMD_READ_CFG    0x20
#define PS2_CMD_WRITE_CFG   0x60
#define PS2_CMD_DISABLE_2   0xA7
#define PS2_CMD_ENABLE_2    0xA8
#define PS2_CMD_AUX_DEV     0xD4

/* ── Mouse commands ─────────────────────────────────────────────────────── */
#define MOUSE_CMD_ENABLE    0xF4
#define MOUSE_CMD_SET_RATE  0xF3
#define MOUSE_CMD_GET_ID    0xF2
#define MOUSE_CMD_RESET     0xFF

/* ── Module state ────────────────────────────────────────────────────────── */
static mouse_state_t state;
static uint8_t packet[3];
static int packet_idx;
static uint8_t prev_buttons;

/* ── PS/2 controller helpers ─────────────────────────────────────────────── */

static void ps2_wait_input(void)
{
    while (inb(PS2_STATUS) & 0x02)
        ;
}

static void ps2_wait_output(void)
{
    while (!(inb(PS2_STATUS) & 0x01))
        ;
}

static void ps2_write_cmd(uint8_t cmd)
{
    ps2_wait_input();
    outb(PS2_CMD, cmd);
}

static void ps2_write_data(uint8_t data)
{
    ps2_wait_input();
    outb(PS2_DATA, data);
}

static uint8_t ps2_read_data(void)
{
    ps2_wait_output();
    return inb(PS2_DATA);
}

static void mouse_write(uint8_t cmd)
{
    ps2_write_cmd(PS2_CMD_AUX_DEV);
    io_wait();
    ps2_write_data(cmd);
    io_wait();
    /* Wait for ACK */
    ps2_read_data();
}

/* ── IRQ12 handler ───────────────────────────────────────────────────────── */

static void mouse_irq_handler(registers_t *regs)
{
    (void)regs;
    uint8_t data = inb(PS2_DATA);

    packet[packet_idx++] = data;

    if (packet_idx >= 3) {
        packet_idx = 0;

        /* Parse packet */
        uint8_t flags = packet[0];

        /* Check sync bit (bit 3 must be set) */
        if (!(flags & 0x08))
            return;

        /* Extract button states */
        state.buttons = flags & 0x07;

        /* Extract X movement (sign-extend from 9 bits) */
        int dx = packet[1];
        if (flags & 0x10)  /* X sign bit */
            dx |= 0xFFFFFF00;

        /* Extract Y movement (sign-extend, and invert for screen coords) */
        int dy = packet[2];
        if (flags & 0x20)  /* Y sign bit */
            dy |= 0xFFFFFF00;
        dy = -dy;  /* Invert Y (PS/2 Y increases upward, screen Y increases downward) */

        /* Update position */
        state.x += dx;
        state.y += dy;

        /* Clamp to screen bounds */
        if (state.x < 0) state.x = 0;
        if (state.y < 0) state.y = 0;
        if (state.x >= SCREEN_W) state.x = SCREEN_W - 1;
        if (state.y >= SCREEN_H) state.y = SCREEN_H - 1;

        state.ready = 1;
    }
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void mouse_init(void)
{
    LOG_INFO("Mouse: initializing PS/2 mouse");

    state.x = SCREEN_W / 2;
    state.y = SCREEN_H / 2;
    state.buttons = 0;
    state.ready = 0;
    packet_idx = 0;
    prev_buttons = 0;

    /* Enable the auxiliary device (mouse) */
    ps2_write_cmd(PS2_CMD_ENABLE_2);
    io_wait();

    /* Read current controller configuration */
    ps2_write_cmd(PS2_CMD_READ_CFG);
    uint8_t cfg = ps2_read_data();

    /* Enable IRQ12 and enable mouse clock */
    cfg |= 0x02;   /* Enable IRQ12 */
    cfg &= ~0x20;  /* Enable mouse clock */

    /* Write back configuration */
    ps2_write_cmd(PS2_CMD_WRITE_CFG);
    io_wait();
    ps2_write_data(cfg);
    io_wait();

    /* Set defaults */
    mouse_write(MOUSE_CMD_RESET);
    io_wait();

    /* Set sample rate (200 reports/sec for smooth movement) */
    mouse_write(MOUSE_CMD_SET_RATE);
    io_wait();
    mouse_write(200);
    io_wait();

    /* Enable data reporting */
    mouse_write(MOUSE_CMD_ENABLE);
    io_wait();

    /* Register IRQ handler (IRQ12 = vector 44, remapped from PIC2) */
    irq_register_handler(12, mouse_irq_handler);
    irq_enable(12);

    LOG_INFO("Mouse: PS/2 mouse ready at (%d, %d)", state.x, state.y);
}

const mouse_state_t *mouse_get(void)
{
    return &state;
}

int mouse_left_clicked(void)
{
    uint8_t cur = state.buttons & 0x01;
    uint8_t prev = prev_buttons & 0x01;
    prev_buttons = state.buttons;
    return cur && !prev;
}

int mouse_left_held(void)
{
    return (state.buttons & 0x01) != 0;
}
