/* gui.h — Desktop environment and window manager */
#pragma once
#include <stdint.h>

/* ── Limits ──────────────────────────────────────────────────────────────── */
#define MAX_WINDOWS     8
#define MAX_TITLE_LEN   32
#define TASKBAR_H       18
#define TITLEBAR_H      12

/* ── Window flags ────────────────────────────────────────────────────────── */
#define WIN_FLAG_VISIBLE  0x01
#define WIN_FLAG_FOCUSED  0x02

/* ── Application types ───────────────────────────────────────────────────── */
typedef enum {
    APP_NONE = 0,
    APP_TERMINAL,
    APP_CLOCK,
    APP_PAINT,
    APP_ABOUT
} app_type_t;

/* ── Terminal buffer ─────────────────────────────────────────────────────── */
#define TERM_COLS   38
#define TERM_ROWS   18
#define TERM_BUF_SZ 2048

typedef struct {
    char    lines[TERM_ROWS][TERM_COLS + 1];
    int     cur_row;
    int     cur_col;
    char    input[TERM_COLS + 1];
    int     input_len;
    uint8_t fg;
    uint8_t bg;
} term_t;

/* ── Paint state ─────────────────────────────────────────────────────────── */
#define PAINT_W  200
#define PAINT_H  140

typedef struct {
    uint8_t canvas[PAINT_H][PAINT_W];
    uint8_t current_color;
    int     prev_mx, prev_my;
} paint_t;

/* ── Window ──────────────────────────────────────────────────────────────── */
typedef struct {
    char     title[MAX_TITLE_LEN];
    int      x, y;
    int      w, h;
    uint8_t  flags;
    app_type_t app;

    /* Application data (union-ish) */
    term_t   term;
    paint_t  paint;
} window_t;

/* ── Public API ──────────────────────────────────────────────────────────── */

/* Initialise the desktop (call after gfx_init, mouse_init, font_init) */
void gui_init(void);

/* Main desktop loop — runs forever (call from kernel_main) */
void gui_run(void);

/* Timer tick — updates clock, redraws (called from timer IRQ) */
void gui_tick(void);

/* Keyboard event — called when a key is pressed */
void gui_key_event(char ch);

/* Create a new window. Returns window index or -1 */
int gui_create_window(const char *title, int w, int h, app_type_t app);

/* Close a window */
void gui_close_window(int idx);

/* Get the focused window index (-1 if none) */
int gui_focused_window(void);
