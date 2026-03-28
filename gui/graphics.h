/* graphics.h — VGA Mode 13h (320x200, 256 colors) graphics driver */
#pragma once
#include <stdint.h>

/* Screen dimensions */
#define SCREEN_W    320
#define SCREEN_H    200

/* ── Standard palette indices (first 32 entries) ──────────────────────────── */
#define COL_BLACK           0
#define COL_DARK_BLUE       1
#define COL_DARK_GREEN      2
#define COL_DARK_CYAN       3
#define COL_DARK_RED        4
#define COL_DARK_MAGENTA    5
#define COL_DARK_YELLOW     6
#define COL_LIGHT_GREY      7
#define COL_DARK_GREY       8
#define COL_BLUE            9
#define COL_GREEN           10
#define COL_CYAN            11
#define COL_RED             12
#define COL_MAGENTA         13
#define COL_YELLOW          14
#define COL_WHITE           15

/* UI colours */
#define COL_DESKTOP_BG      1     /* dark blue desktop */
#define COL_TASKBAR_BG      8     /* dark grey */
#define COL_TASKBAR_TEXT    15    /* white */
#define COL_TITLE_ACTIVE    9     /* blue */
#define COL_TITLE_INACTIVE  8     /* dark grey */
#define COL_WINDOW_BG       11    /* cyan-ish */
#define COL_BORDER          15    /* white */
#define COL_TERMINAL_BG     0     /* black */
#define COL_TERMINAL_TEXT    10   /* green */
#define COL_CURSOR          15    /* white */

/* ── API ─────────────────────────────────────────────────────────────────── */

/* Switch VGA to Mode 13h and clear screen */
void gfx_init(void);

/* Flip the double-buffer to the VGA framebuffer */
void gfx_flush(void);

/* ── Drawing primitives (draw into back buffer) ──────────────────────────── */
void gfx_pixel(int x, int y, uint8_t color);
void gfx_fill_rect(int x, int y, int w, int h, uint8_t color);
void gfx_rect(int x, int y, int w, int h, uint8_t color);
void gfx_hline(int x, int y, int w, uint8_t color);
void gfx_vline(int x, int y, int h, uint8_t color);
void gfx_clear(uint8_t color);

/* Draw a character at (x,y) with the given fg/bg colours */
void gfx_char(int x, int y, char c, uint8_t fg, uint8_t bg);

/* Draw a null-terminated string */
void gfx_string(int x, int y, const char *s, uint8_t fg, uint8_t bg);

/* ── Palette ─────────────────────────────────────────────────────────────── */
void gfx_set_palette(void);
