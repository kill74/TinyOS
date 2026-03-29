/* graphics.c — VGA Mode 13h (320x200, 256 colors) driver
 *
 * Mode 13h maps 64 KB of video memory at physical 0xA0000.
 * Each byte is one pixel; the value indexes into a 256-entry colour palette.
 * We use a back-buffer and flip via memcpy for flicker-free rendering.
 */

#include "../gui/graphics.h"
#include "../gui/font.h"
#include "../include/log.h"
#include <stdint.h>

/* ── I/O helpers (defined in helpers.S) ──────────────────────────────────── */
extern void outb(uint16_t port, uint8_t value);
extern uint8_t inb(uint16_t port);

/* ── VGA ports ───────────────────────────────────────────────────────────── */
#define VGA_MISC_WRITE   0x3C2
#define VGA_SEQ_INDEX    0x3C4
#define VGA_SEQ_DATA     0x3C5
#define VGA_CRTC_INDEX   0x3D4
#define VGA_CRTC_DATA    0x3D5
#define VGA_GC_INDEX     0x3CE
#define VGA_GC_DATA      0x3CF
#define VGA_AC_INDEX     0x3C0
#define VGA_AC_WRITE     0x3C0
#define VGA_AC_READ      0x3C1
#define VGA_DAC_WRITE    0x3C8
#define VGA_DAC_DATA     0x3C9
#define VGA_INSTAT_READ  0x3DA

/* ── Mode 13h register tables ────────────────────────────────────────────── */

static const uint8_t misc[] = { 0x63 };

static const uint8_t seq[] = {
    0x03, 0x01, 0x0F, 0x00, 0x0E
};

static const uint8_t crtc[] = {
    0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0xBF, 0x1F,
    0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x9C, 0x0E, 0x8F, 0x28, 0x40, 0x96, 0xB9, 0xA3,
    0xFF
};

static const uint8_t gc[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0F,
    0xFF
};

static const uint8_t ac[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x41, 0x00, 0x0F, 0x00, 0x00
};

/* ── Buffers ─────────────────────────────────────────────────────────────── */
static uint8_t *vga_mem = (uint8_t *)0xA0000;
static uint8_t back_buf[SCREEN_W * SCREEN_H];

/* ── Palette generation ──────────────────────────────────────────────────── */

static void set_palette_entry(uint8_t idx, uint8_t r, uint8_t g, uint8_t b)
{
    outb(VGA_DAC_WRITE, idx);
    outb(VGA_DAC_DATA, r >> 2);
    outb(VGA_DAC_DATA, g >> 2);
    outb(VGA_DAC_DATA, b >> 2);
}

static void init_palette(void)
{
    /* Standard first 16 colours (CGA-compatible) */
    static const uint8_t pal16[16][3] = {
        {  0,   0,   0}, {  0,   0, 170}, {  0, 170,   0}, {  0, 170, 170},
        {170,   0,   0}, {170,   0, 170}, {170, 170,   0}, {170, 170, 170},
        { 85,  85,  85}, { 85,  85, 255}, { 85, 255,  85}, { 85, 255, 255},
        {255,  85,  85}, {255,  85, 255}, {255, 255,  85}, {255, 255, 255},
    };

    for (int i = 0; i < 16; i++)
        set_palette_entry(i, pal16[i][0], pal16[i][1], pal16[i][2]);

    /* Colours 16-31: refined UI palette */
    set_palette_entry(16,  16,  16,  24);   /* 16: window bg / deep shadow  */
    set_palette_entry(17,  32,  36,  52);   /* 17: desktop background       */
    set_palette_entry(18,  48,  52,  72);   /* 18: icon bg / panel          */
    set_palette_entry(19,  28,  68,  32);   /* 19: dark green               */
    set_palette_entry(20, 180,  56,  56);   /* 20: accent red               */
    set_palette_entry(21,  88,  88,  48);   /* 21: olive                    */
    set_palette_entry(22,  42,  62, 140);   /* 22: title bar active (blue)  */
    set_palette_entry(23, 120,  72,  36);   /* 23: brown                    */
    set_palette_entry(24,  72,  72,  82);   /* 24: taskbar / inactive       */
    set_palette_entry(25, 120, 120, 132);   /* 25: bevel highlight / light  */
    set_palette_entry(26,  68, 108, 176);   /* 26: title bar highlight      */
    set_palette_entry(27,  92, 172,  92);   /* 27: success green            */
    set_palette_entry(28, 200,  80,  80);   /* 28: salmon                   */
    set_palette_entry(29, 180,  72, 180);   /* 29: pink                     */
    set_palette_entry(30, 200, 200, 100);   /* 30: light yellow             */
    set_palette_entry(31, 212, 212, 220);   /* 31: near-white (win bg)      */

    /* Colours 32-255: 6x6x6 colour cube + greys */
    for (int i = 32; i < 256; i++) {
        int idx = i - 32;
        if (idx < 216) {
            uint8_t r = (uint8_t)((idx / 36) * 51);
            uint8_t g = (uint8_t)(((idx / 6) % 6) * 51);
            uint8_t b = (uint8_t)((idx % 6) * 51);
            set_palette_entry(i, r, g, b);
        } else {
            uint8_t v = (uint8_t)((idx - 216) * 10);
            set_palette_entry(i, v, v, v);
        }
    }
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void gfx_init(void)
{
    LOG_INFO("Graphics: switching to VGA Mode 13h (320x200)");

    /* Disable display during mode switch */
    inb(VGA_INSTAT_READ);
    outb(VGA_AC_INDEX, 0x00);

    /* Write registers */
    outb(VGA_MISC_WRITE, misc[0]);

    for (int i = 0; i < 5; i++) {
        outb(VGA_SEQ_INDEX, i);
        outb(VGA_SEQ_DATA, seq[i]);
    }

    /* Unlock CRTC registers (bit 7 of index 0x11) */
    outb(VGA_CRTC_INDEX, 0x11);
    outb(VGA_CRTC_DATA, inb(VGA_CRTC_DATA) & 0x7F);

    for (int i = 0; i < 25; i++) {
        outb(VGA_CRTC_INDEX, i);
        outb(VGA_CRTC_DATA, crtc[i]);
    }

    for (int i = 0; i < 9; i++) {
        outb(VGA_GC_INDEX, i);
        outb(VGA_GC_DATA, gc[i]);
    }

    for (int i = 0; i < 21; i++) {
        inb(VGA_INSTAT_READ);
        outb(VGA_AC_INDEX, i);
        outb(VGA_AC_WRITE, ac[i]);
    }

    /* Re-enable display */
    inb(VGA_INSTAT_READ);
    outb(VGA_AC_INDEX, 0x20);

    /* Set up palette */
    init_palette();

    /* Clear back buffer */
    gfx_clear(COL_DESKTOP_BG);

    LOG_INFO("Graphics: Mode 13h ready");
}

void gfx_flush(void)
{
    /* Copy back buffer to VGA memory */
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++)
        vga_mem[i] = back_buf[i];
}

/* ── Drawing primitives ──────────────────────────────────────────────────── */

void gfx_pixel(int x, int y, uint8_t color)
{
    if (x >= 0 && x < SCREEN_W && y >= 0 && y < SCREEN_H)
        back_buf[y * SCREEN_W + x] = color;
}

void gfx_fill_rect(int x, int y, int w, int h, uint8_t color)
{
    int x1 = x < 0 ? 0 : x;
    int y1 = y < 0 ? 0 : y;
    int x2 = (x + w) > SCREEN_W ? SCREEN_W : (x + w);
    int y2 = (y + h) > SCREEN_H ? SCREEN_H : (y + h);

    for (int row = y1; row < y2; row++) {
        uint8_t *p = &back_buf[row * SCREEN_W + x1];
        for (int col = x1; col < x2; col++)
            *p++ = color;
    }
}

void gfx_rect(int x, int y, int w, int h, uint8_t color)
{
    gfx_hline(x, y, w, color);
    gfx_hline(x, y + h - 1, w, color);
    gfx_vline(x, y, h, color);
    gfx_vline(x + w - 1, y, h, color);
}

void gfx_hline(int x, int y, int w, uint8_t color)
{
    if (y < 0 || y >= SCREEN_H) return;
    for (int i = 0; i < w; i++) {
        int px = x + i;
        if (px >= 0 && px < SCREEN_W)
            back_buf[y * SCREEN_W + px] = color;
    }
}

void gfx_vline(int x, int y, int h, uint8_t color)
{
    if (x < 0 || x >= SCREEN_W) return;
    for (int i = 0; i < h; i++) {
        int py = y + i;
        if (py >= 0 && py < SCREEN_H)
            back_buf[py * SCREEN_W + x] = color;
    }
}

void gfx_clear(uint8_t color)
{
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++)
        back_buf[i] = color;
}

void gfx_char(int x, int y, char c, uint8_t fg, uint8_t bg)
{
    font_draw_char(x, y, c, fg, bg);
}

void gfx_string(int x, int y, const char *s, uint8_t fg, uint8_t bg)
{
    font_draw_string(x, y, s, fg, bg);
}

void gfx_set_palette(void)
{
    init_palette();
}
