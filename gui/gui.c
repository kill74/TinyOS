/* gui.c — Desktop environment with window manager, apps, and mouse support
 *
 * Launches as a graphical desktop with:
 *   - Draggable windows with title bars and close buttons
 *   - Taskbar with app icons
 *   - Terminal app (embedded shell)
 *   - Clock app
 *   - Paint app
 *   - About dialog
 */

#include "../gui/gui.h"
#include "../gui/graphics.h"
#include "../gui/mouse.h"
#include "../gui/font.h"
#include "../include/log.h"
#include "../include/timer.h"
#include "../include/keyboard.h"
#include "../fs/fs.h"
#include <stdint.h>

/* ── Local memcpy (no libc) ──────────────────────────────────────────────── */
static void *gui_memcpy(void *dst, const void *src, int n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (int i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

/* ── Colours (palette indices) ───────────────────────────────────────────── */
#define C_DESKTOP       17    /* dark blue-grey */
#define C_TASKBAR       24    /* grey */
#define C_TASKBAR_HI    25    /* light grey */
#define C_TITLE_ACT     22    /* royal blue */
#define C_TITLE_INACT   24    /* grey */
#define C_WIN_BG        16    /* very dark */
#define C_BORDER        15    /* white */
#define C_CLOSE_BTN     12    /* red */
#define C_CLOSE_HI      COL_WHITE
#define C_TERMINAL_BG   0     /* black */
#define C_TERMINAL_FG   10    /* green */
#define C_TERMINAL_PR   15    /* white prompt */
#define C_PAINT_BG      15    /* white canvas */
#define C_TEXT          15    /* white */
#define C_TEXT_DIM      7     /* light grey */
#define C_ICON_BG       18    /* medium blue-grey */
#define C_ICON_HI       26    /* sky blue */

/* ── Module state ────────────────────────────────────────────────────────── */
static window_t wins[MAX_WINDOWS];
static int win_count;
static int focused_idx;
static int drag_win;       /* -1 = not dragging */
static int drag_ox, drag_oy;
static int tick_counter;
static int needs_redraw;

/* ── App icon positions on desktop ───────────────────────────────────────── */
typedef struct {
    const char *label;
    app_type_t app;
    int x, y;
} desktop_icon_t;

static const desktop_icon_t icons[] = {
    { "Terminal",  APP_TERMINAL,  10,  20 },
    { "Clock",     APP_CLOCK,     10,  70 },
    { "Paint",     APP_PAINT,     10, 120 },
    { "About",     APP_ABOUT,     10, 170 },
};
#define N_ICONS 4

/* ── Forward declarations ────────────────────────────────────────────────── */
static void win_draw(window_t *w, int focused);
static void term_init(term_t *t);
static void term_putchar(term_t *t, char c);
static void term_exec(term_t *t, const char *cmd);
static void paint_init(paint_t *p);

/* ── Window management ───────────────────────────────────────────────────── */

int gui_create_window(const char *title, int w, int h, app_type_t app)
{
    if (win_count >= MAX_WINDOWS) return -1;

    int idx = win_count++;
    window_t *win = &wins[idx];

    /* Copy title */
    int i;
    for (i = 0; i < MAX_TITLE_LEN - 1 && title[i]; i++)
        win->title[i] = title[i];
    win->title[i] = '\0';

    /* Center window on desktop area (above taskbar) */
    int desktop_h = SCREEN_H - TASKBAR_H;
    win->x = (SCREEN_W - w) / 2 + idx * 15;
    win->y = (desktop_h - h - TITLEBAR_H) / 2 + idx * 10;
    if (win->x < 0) win->x = 5;
    if (win->y < 0) win->y = 5;

    win->w = w;
    win->h = h;
    win->flags = WIN_FLAG_VISIBLE;
    win->app = app;

    /* Initialise app data */
    switch (app) {
        case APP_TERMINAL: term_init(&win->term); break;
        case APP_PAINT:    paint_init(&win->paint); break;
        default: break;
    }

    focused_idx = idx;
    needs_redraw = 1;
    return idx;
}

void gui_close_window(int idx)
{
    if (idx < 0 || idx >= win_count) return;

    /* Shift windows down (manual copy to avoid libc memcpy) */
    for (int i = idx; i < win_count - 1; i++)
        gui_memcpy(&wins[i], &wins[i + 1], sizeof(window_t));
    win_count--;

    if (focused_idx >= win_count)
        focused_idx = win_count - 1;
    needs_redraw = 1;
}

int gui_focused_window(void)
{
    return focused_idx;
}

/* ── Drawing ─────────────────────────────────────────────────────────────── */

static void draw_mouse_cursor(int mx, int my)
{
    /* Simple arrow cursor (8x12) */
    static const uint8_t cursor_shape[12] = {
        0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE, 0xFF,
        0xFC, 0xDC, 0x8E, 0x06,
    };
    static const uint8_t cursor_mask[12] = {
        0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE, 0xFF,
        0xFC, 0xFC, 0xFE, 0xFF,
    };

    for (int row = 0; row < 12; row++) {
        for (int col = 0; col < 8; col++) {
            int px = mx + col;
            int py = my + row;
            if (px < 0 || px >= SCREEN_W || py < 0 || py >= SCREEN_H)
                continue;

            if (cursor_mask[row] & (0x80 >> col)) {
                uint8_t c = (cursor_shape[row] & (0x80 >> col)) ? COL_WHITE : COL_BLACK;
                gfx_pixel(px, py, c);
            }
        }
    }
}

static void draw_taskbar(void)
{
    int tb_y = SCREEN_H - TASKBAR_H;

    /* Taskbar background */
    gfx_fill_rect(0, tb_y, SCREEN_W, TASKBAR_H, C_TASKBAR);

    /* Top border */
    gfx_hline(0, tb_y, SCREEN_W, C_BORDER);

    /* App icons on taskbar */
    const char *app_labels[] = { "Term", "Clock", "Paint", "About" };
    int x = 5;
    for (int i = 0; i < 4; i++) {
        gfx_fill_rect(x, tb_y + 2, 42, 14, C_ICON_BG);
        gfx_rect(x, tb_y + 2, 42, 14, C_TASKBAR_HI);
        font_draw_string(x + 3, tb_y + 5, app_labels[i], C_TEXT, C_ICON_BG);
        x += 46;
    }

    /* Clock on right side of taskbar */
    uint32_t ticks = timer_get_ticks();
    uint32_t secs = ticks / 100;
    uint32_t mins = secs / 60;
    uint32_t hrs = (mins / 60) % 24;
    mins %= 60;
    secs %= 60;

    char time_str[9];
    time_str[0] = '0' + (hrs / 10);
    time_str[1] = '0' + (hrs % 10);
    time_str[2] = ':';
    time_str[3] = '0' + (mins / 10);
    time_str[4] = '0' + (mins % 10);
    time_str[5] = ':';
    time_str[6] = '0' + (secs / 10);
    time_str[7] = '0' + (secs % 10);
    time_str[8] = '\0';

    font_draw_string(SCREEN_W - 60, tb_y + 5, time_str, C_TEXT, C_TASKBAR);
}

static void draw_desktop_icons(void)
{
    for (int i = 0; i < N_ICONS; i++) {
        int ix = icons[i].x;
        int iy = icons[i].y;

        /* Icon background */
        gfx_fill_rect(ix, iy, 40, 36, C_ICON_BG);
        gfx_rect(ix, iy, 40, 36, C_ICON_HI);

        /* Icon symbol (simple coloured square) */
        uint8_t sym_col = (i == 0) ? C_TERMINAL_FG :
                          (i == 1) ? COL_YELLOW :
                          (i == 2) ? COL_RED : COL_CYAN;
        gfx_fill_rect(ix + 12, iy + 4, 16, 16, sym_col);

        /* Label */
        font_draw_string(ix + 2, iy + 24, icons[i].label, C_TEXT, C_ICON_BG);
    }
}

static void draw_window(window_t *w, int focused)
{
    if (!(w->flags & WIN_FLAG_VISIBLE)) return;

    int x = w->x;
    int y = w->y;
    int wi = w->w;
    int hi = w->h + TITLEBAR_H;

    /* Shadow */
    gfx_fill_rect(x + 3, y + 3, wi, hi, COL_BLACK);

    /* Window background */
    gfx_fill_rect(x, y, wi, hi, C_WIN_BG);

    /* Title bar */
    uint8_t tc = focused ? C_TITLE_ACT : C_TITLE_INACT;
    gfx_fill_rect(x, y, wi, TITLEBAR_H, tc);

    /* Title text */
    font_draw_string(x + 4, y + 3, w->title, C_TEXT, tc);

    /* Close button */
    gfx_fill_rect(x + wi - 11, y + 2, 9, 8, C_CLOSE_BTN);
    font_draw_string(x + wi - 9, y + 3, "x", C_CLOSE_HI, C_CLOSE_BTN);

    /* Border */
    gfx_rect(x, y, wi, hi, C_BORDER);

    /* Draw app content */
    win_draw(w, focused);
}

/* ── App rendering ───────────────────────────────────────────────────────── */

static void win_draw(window_t *w, int focused)
{
    (void)focused;
    int cx = w->x + 1;
    int cy = w->y + TITLEBAR_H + 1;
    int cw = w->w - 2;
    int ch = w->h - 2;

    switch (w->app) {
    case APP_TERMINAL: {
        /* Terminal background */
        gfx_fill_rect(cx, cy, cw, ch, C_TERMINAL_BG);

        /* Draw lines of text */
        for (int row = 0; row < TERM_ROWS && row * FONT_H < ch; row++) {
            int ly = cy + row * FONT_H;
            if (ly + FONT_H > cy + ch) break;

            /* Current line (input) */
            if (row == w->term.cur_row) {
                /* Show prompt + input */
                char line[TERM_COLS + 1];
                line[0] = '>';
                line[1] = ' ';
                int len = w->term.input_len < TERM_COLS - 2 ? w->term.input_len : TERM_COLS - 2;
                for (int i = 0; i < len; i++)
                    line[2 + i] = w->term.input[i];
                int total = 2 + len;
                for (int i = total; i < TERM_COLS; i++)
                    line[i] = ' ';
                line[TERM_COLS] = '\0';
                font_draw_string(cx + 2, ly, line, C_TERMINAL_FG, C_TERMINAL_BG);
            } else {
                /* History line */
                const char *ln = w->term.lines[row];
                font_draw_string(cx + 2, ly, ln, C_TERMINAL_FG, C_TERMINAL_BG);
            }
        }

        /* Blinking cursor */
        if (focused && (tick_counter / 25) % 2) {
            int cur_x = cx + 2 + (2 + w->term.input_len) * FONT_W;
            int cur_y = cy + w->term.cur_row * FONT_H;
            if (cur_x < cx + cw - FONT_W)
                gfx_fill_rect(cur_x, cur_y, FONT_W, FONT_H, C_TERMINAL_FG);
        }
        break;
    }

    case APP_CLOCK: {
        gfx_fill_rect(cx, cy, cw, ch, C_WIN_BG);

        uint32_t ticks = timer_get_ticks();
        uint32_t secs = ticks / 100;
        uint32_t mins = secs / 60;
        uint32_t hrs = (mins / 60) % 24;
        mins %= 60;
        secs %= 60;

        /* Large time display */
        char buf[16];
        buf[0] = '0' + (hrs / 10);
        buf[1] = '0' + (hrs % 10);
        buf[2] = ':';
        buf[3] = '0' + (mins / 10);
        buf[4] = '0' + (mins % 10);
        buf[5] = ':';
        buf[6] = '0' + (secs / 10);
        buf[7] = '0' + (secs % 10);
        buf[8] = '\0';

        /* Draw time centered, scaled up (2x) */
        int tx = cx + (cw - 8 * FONT_W * 2) / 2;
        int ty = cy + (ch - FONT_H * 2) / 2;
        for (int i = 0; buf[i]; i++) {
            const uint8_t *glyph = font_get_glyph(buf[i]);
            for (int row = 0; row < FONT_H; row++) {
                for (int col = 0; col < FONT_W; col++) {
                    if (glyph[row] & (0x80 >> col)) {
                        gfx_fill_rect(tx + i * FONT_W * 2 + col * 2,
                                      ty + row * 2, 2, 2, COL_CYAN);
                    }
                }
            }
        }

        /* Tick count below */
        char ts[16];
        int t = (int)ticks;
        int p = 0;
        char tmp[12];
        if (t == 0) { tmp[0] = '0'; p = 1; }
        else {
            while (t > 0 && p < 12) { tmp[p++] = '0' + (t % 10); t /= 10; }
        }
        int pos = 0;
        ts[pos++] = 'T'; ts[pos++] = 'i'; ts[pos++] = 'c'; ts[pos++] = 'k';
        ts[pos++] = 's'; ts[pos++] = ':';
        ts[pos++] = ' ';
        for (int i = p - 1; i >= 0; i--) ts[pos++] = tmp[i];
        ts[pos] = '\0';
        font_draw_string(cx + 8, ty + FONT_H * 2 + 10, ts, C_TEXT_DIM, C_WIN_BG);
        break;
    }

    case APP_PAINT: {
        paint_t *p = &w->paint;

        /* Canvas */
        int canvas_x = cx + 2;
        int canvas_y = cy + 2;
        int canvas_w = cw - 4;
        int canvas_h = ch - 22;
        if (canvas_w > PAINT_W) canvas_w = PAINT_W;
        if (canvas_h > PAINT_H) canvas_h = PAINT_H;

        gfx_rect(canvas_x - 1, canvas_y - 1, canvas_w + 2, canvas_h + 2, C_BORDER);

        /* Draw canvas pixels */
        for (int py = 0; py < canvas_h; py++) {
            for (int px = 0; px < canvas_w; px++) {
                gfx_pixel(canvas_x + px, canvas_y + py, p->canvas[py][px]);
            }
        }

        /* Palette at bottom */
        int pal_y = cy + ch - 16;
        for (int i = 0; i < 16; i++) {
            gfx_fill_rect(cx + 4 + i * 12, pal_y, 10, 10, (uint8_t)(i + 32));
            if (i == p->current_color)
                gfx_rect(cx + 4 + i * 12, pal_y, 10, 10, C_BORDER);
        }
        break;
    }

    case APP_ABOUT: {
        gfx_fill_rect(cx, cy, cw, ch, C_WIN_BG);
        font_draw_string(cx + 10, cy + 10, "TinyOS v0.1", C_TERMINAL_FG, C_WIN_BG);
        font_draw_string(cx + 10, cy + 22, "A minimal x86 kernel", C_TEXT_DIM, C_WIN_BG);
        font_draw_string(cx + 10, cy + 38, "with GUI, networking,", C_TEXT_DIM, C_WIN_BG);
        font_draw_string(cx + 10, cy + 50, "and TCP/IP stack.", C_TEXT_DIM, C_WIN_BG);
        font_draw_string(cx + 10, cy + 70, "Built with <3", C_TERMINAL_FG, C_WIN_BG);
        break;
    }

    default:
        break;
    }
}

/* ── Terminal app ────────────────────────────────────────────────────────── */

static void term_init(term_t *t)
{
    for (int r = 0; r < TERM_ROWS; r++)
        for (int c = 0; c <= TERM_COLS; c++)
            t->lines[r][c] = '\0';
    t->cur_row = 0;
    t->cur_col = 0;
    t->input_len = 0;
    t->input[0] = '\0';
    t->fg = C_TERMINAL_FG;
    t->bg = 0;

    /* Welcome message */
    const char *w1 = "TinyOS Terminal v0.1";
    const char *w2 = "Type 'help' for commands";
    int i;
    for (i = 0; w1[i] && i < TERM_COLS; i++) t->lines[0][i] = w1[i];
    t->lines[0][i] = '\0';
    for (i = 0; w2[i] && i < TERM_COLS; i++) t->lines[1][i] = w2[i];
    t->lines[1][i] = '\0';
    t->cur_row = 2;
}

static void term_scroll(term_t *t)
{
    for (int r = 0; r < TERM_ROWS - 1; r++)
        for (int c = 0; c <= TERM_COLS; c++)
            t->lines[r][c] = t->lines[r + 1][c];
    t->lines[TERM_ROWS - 1][0] = '\0';
    if (t->cur_row > 0) t->cur_row--;
}

static void term_newline(term_t *t)
{
    /* Save current input line to history */
    char line[TERM_COLS + 1];
    line[0] = '>';
    line[1] = ' ';
    int len = t->input_len < TERM_COLS - 2 ? t->input_len : TERM_COLS - 2;
    for (int i = 0; i < len; i++)
        line[2 + i] = t->input[i];
    line[2 + len] = '\0';

    /* Write to current row */
    int i;
    for (i = 0; line[i] && i < TERM_COLS; i++)
        t->lines[t->cur_row][i] = line[i];
    t->lines[t->cur_row][i] = '\0';

    t->cur_row++;
    if (t->cur_row >= TERM_ROWS) term_scroll(t);

    /* Execute command */
    if (t->input_len > 0)
        term_exec(t, t->input);

    t->input_len = 0;
    t->input[0] = '\0';
}

static void term_putchar(term_t *t, char c)
{
    if (c == '\n' || c == '\r') {
        term_newline(t);
    } else if (c == '\b') {
        if (t->input_len > 0)
            t->input[--t->input_len] = '\0';
    } else if (t->input_len < TERM_COLS - 3) {
        t->input[t->input_len++] = c;
        t->input[t->input_len] = '\0';
    }
}

static void term_output(term_t *t, const char *msg)
{
    /* Write output lines to the terminal */
    int row = t->cur_row + 1;
    if (row >= TERM_ROWS) { term_scroll(t); row = TERM_ROWS - 1; }

    int col = 0;
    for (int i = 0; msg[i]; i++) {
        if (msg[i] == '\n' || col >= TERM_COLS) {
            t->lines[row][col] = '\0';
            row++;
            if (row >= TERM_ROWS) { term_scroll(t); row = TERM_ROWS - 1; }
            col = 0;
            if (msg[i] == '\n') continue;
        }
        t->lines[row][col++] = msg[i];
    }
    t->lines[row][col] = '\0';
}

static char *gui_ls_buf_ptr;

static int gui_ls_cb(const fs_stat_t *st, void *ctx)
{
    (void)ctx;
    char *p = gui_ls_buf_ptr;
    const char *tp = (st->type == FS_TYPE_DIR) ? "D " : "F ";
    *p++ = tp[0]; *p++ = tp[1];
    char sz[8]; int n = 0; unsigned v = st->size;
    if (v == 0) { sz[n++] = '0'; }
    else { while (v > 0 && n < 8) { sz[n++] = '0' + (v % 10); v /= 10; } }
    for (int i = n - 1; i >= 0; i--) *p++ = sz[i];
    *p++ = ' ';
    for (int i = 0; st->name[i] && i < 32; i++) *p++ = st->name[i];
    *p++ = '\n';
    *p = '\0';
    gui_ls_buf_ptr = p;
    return 0;
}

static void term_exec(term_t *t, const char *cmd)
{
    if (cmd[0] == '\0') return;

    if (cmd[0] == 'h' && cmd[1] == 'e' && cmd[2] == 'l' && cmd[3] == 'p' && cmd[4] == '\0') {
        term_output(t, "Commands:\nhelp echo clear ver\nmem date ls touch\nrm cat write fsinfo");
    } else if (cmd[0] == 'v' && cmd[1] == 'e' && cmd[2] == 'r' && cmd[3] == '\0') {
        term_output(t, "TinyOS v0.1\nGUI Desktop Edition");
    } else if (cmd[0] == 'c' && cmd[1] == 'l' && cmd[2] == 'e' &&
               cmd[3] == 'a' && cmd[4] == 'r' && cmd[5] == '\0') {
        term_init(t);
    } else if (cmd[0] == 'm' && cmd[1] == 'e' && cmd[2] == 'm' && cmd[3] == '\0') {
        term_output(t, "Heap: 2MB pool");
    } else if (cmd[0] == 'd' && cmd[1] == 'a' && cmd[2] == 't' && cmd[3] == 'e' && cmd[4] == '\0') {
        uint32_t ticks = timer_get_ticks();
        uint32_t s = ticks / 100;
        uint32_t m = s / 60;
        uint32_t h = (m / 60) % 24;
        m %= 60; s %= 60;
        char buf[16];
        buf[0] = '0' + (h/10); buf[1] = '0' + (h%10); buf[2] = ':';
        buf[3] = '0' + (m/10); buf[4] = '0' + (m%10); buf[5] = ':';
        buf[6] = '0' + (s/10); buf[7] = '0' + (s%10); buf[8] = '\0';
        term_output(t, buf);
    } else if (cmd[0] == 'e' && cmd[1] == 'c' && cmd[2] == 'h' && cmd[3] == 'o' && cmd[4] == ' ') {
        term_output(t, cmd + 5);
    } else if (cmd[0] == 'l' && cmd[1] == 's' && cmd[2] == '\0') {
        static char ls_buf[512];
        ls_buf[0] = '\0';
        gui_ls_buf_ptr = ls_buf;
        fs_ls(gui_ls_cb, NULL);
        if (ls_buf[0] == '\0') term_output(t, "(empty)");
        else term_output(t, ls_buf);
    } else if (cmd[0] == 't' && cmd[1] == 'o' && cmd[2] == 'u' && cmd[3] == 'c' && cmd[4] == 'h' && cmd[5] == ' ') {
        int r = fs_create(cmd + 6, FS_TYPE_FILE);
        if (r == FS_OK) term_output(t, "Created");
        else if (r == FS_ERR_EXIST) term_output(t, "Exists");
        else term_output(t, "Error");
    } else if (cmd[0] == 'r' && cmd[1] == 'm' && cmd[2] == ' ') {
        int r = fs_unlink(cmd + 3);
        if (r == FS_OK) term_output(t, "Deleted");
        else term_output(t, "Not found");
    } else if (cmd[0] == 'c' && cmd[1] == 'a' && cmd[2] == 't' && cmd[3] == ' ') {
        int fd = fs_open(cmd + 4, 0);
        if (fd >= 0) {
            char buf[257];
            int n;
            while ((n = fs_read(fd, buf, 256)) > 0) {
                buf[n] = '\0';
                term_output(t, buf);
            }
            fs_close(fd);
        } else {
            term_output(t, "Not found");
        }
    } else if (cmd[0] == 'w' && cmd[1] == 'r' && cmd[2] == 'i' && cmd[3] == 't' &&
               cmd[4] == 'e' && cmd[5] == ' ') {
        const char *rest = cmd + 6;
        const char *sp = rest;
        while (*sp && *sp != ' ') sp++;
        if (*sp == ' ') {
            char fname[32];
            int i;
            for (i = 0; i < 31 && rest + i < sp; i++) fname[i] = rest[i];
            fname[i] = '\0';
            const char *text = sp + 1;
            int tlen = 0; while (text[tlen]) tlen++;

            int ino = fs_open(fname, 2);
            if (ino < 0) { fs_create(fname, FS_TYPE_FILE); ino = fs_open(fname, 2); }
            if (ino >= 0) {
                fs_seek(ino, 0, FS_SEEK_END);
                fs_write(ino, text, tlen);
                fs_write(ino, "\n", 1);
                fs_close(ino);
                term_output(t, "Written");
            } else {
                term_output(t, "Failed");
            }
        }
    } else if (cmd[0] == 'f' && cmd[1] == 's' && cmd[2] == 'i' && cmd[3] == 'n' &&
               cmd[4] == 'f' && cmd[5] == 'o' && cmd[6] == '\0') {
        fs_stats();
    } else {
        term_output(t, "Unknown. Type 'help'");
    }
}

/* Paint app ───────────────────────────────────────────────────────────── */

static void paint_init(paint_t *p)
{
    for (int y = 0; y < PAINT_H; y++)
        for (int x = 0; x < PAINT_W; x++)
            p->canvas[y][x] = C_PAINT_BG;
    p->current_color = 12; /* red */
    p->prev_mx = -1;
    p->prev_my = -1;
}

/* ── Hit testing ─────────────────────────────────────────────────────────── */

static int point_in_rect(int px, int py, int rx, int ry, int rw, int rh)
{
    return px >= rx && px < rx + rw && py >= ry && py < ry + rh;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void gui_init(void)
{
    win_count = 0;
    focused_idx = -1;
    drag_win = -1;
    tick_counter = 0;
    needs_redraw = 1;

    LOG_INFO("GUI: desktop environment initialized");
}

void gui_run(void)
{
    /* Launch default windows */
    gui_create_window("Terminal", 200, 145, APP_TERMINAL);
    gui_create_window("About", 160, 95, APP_ABOUT);

    LOG_INFO("GUI: entering desktop loop");

    while (1) {
        /* ── Mouse handling ──────────────────────────────────────────── */
        const mouse_state_t *ms = mouse_get();
        int mx = ms->x;
        int my = ms->y;

        if (ms->ready) {
            /* Click handling */
            if (mouse_left_clicked()) {
                /* Check taskbar icon clicks */
                int tb_y = SCREEN_H - TASKBAR_H;
                int ix = 5;
                const app_type_t apps[] = { APP_TERMINAL, APP_CLOCK, APP_PAINT, APP_ABOUT };
                const char *titles[] = { "Terminal", "Clock", "Paint", "About" };
                int dims[][2] = { {200,145}, {160,100}, {210,175}, {160,95} };

                for (int i = 0; i < 4; i++) {
                    if (point_in_rect(mx, my, ix, tb_y + 2, 42, 14)) {
                        /* Open or focus this app */
                        int found = -1;
                        for (int w = 0; w < win_count; w++) {
                            if (wins[w].app == apps[i]) { found = w; break; }
                        }
                        if (found >= 0) {
                            focused_idx = found;
                        } else {
                            gui_create_window(titles[i], dims[i][0], dims[i][1], apps[i]);
                        }
                        needs_redraw = 1;
                        break;
                    }
                    ix += 46;
                }

                /* Check desktop icon clicks */
                for (int i = 0; i < N_ICONS; i++) {
                    if (point_in_rect(mx, my, icons[i].x, icons[i].y, 40, 36)) {
                        int found = -1;
                        for (int w = 0; w < win_count; w++) {
                            if (wins[w].app == icons[i].app) { found = w; break; }
                        }
                        if (found >= 0) {
                            focused_idx = found;
                        } else {
                            gui_create_window(icons[i].label,
                                dims[i][0], dims[i][1], icons[i].app);
                        }
                        needs_redraw = 1;
                        break;
                    }
                }

                /* Check window close buttons and focusing */
                for (int w = win_count - 1; w >= 0; w--) {
                    window_t *win = &wins[w];
                    int wx = win->x;
                    int wy = win->y;
                    int wi = win->w;
                    int hi = win->h + TITLEBAR_H;

                    /* Close button */
                    if (point_in_rect(mx, my, wx + wi - 11, wy + 2, 9, 8)) {
                        gui_close_window(w);
                        needs_redraw = 1;
                        break;
                    }

                    /* Click on window → focus */
                    if (point_in_rect(mx, my, wx, wy, wi, hi)) {
                        if (w != focused_idx) {
                            focused_idx = w;
                            needs_redraw = 1;
                        }

                        /* Start drag on title bar */
                        if (point_in_rect(mx, my, wx, wy, wi, TITLEBAR_H)) {
                            drag_win = w;
                            drag_ox = mx - wx;
                            drag_oy = my - wy;
                        }

                        /* Paint app: colour palette click */
                        if (win->app == APP_PAINT) {
                            int cx = wx + 1;
                            int cy = wy + TITLEBAR_H + 1;
                            int ch = win->h - 2;
                            int pal_y = cy + ch - 16;
                            for (int i = 0; i < 16; i++) {
                                if (point_in_rect(mx, my, cx + 4 + i * 12, pal_y, 10, 10)) {
                                    win->paint.current_color = (uint8_t)(i + 32);
                                    needs_redraw = 1;
                                }
                            }
                        }
                        break;
                    }
                }
            }

            /* Drag window */
            if (drag_win >= 0) {
                if (ms->buttons & 0x01) {
                    wins[drag_win].x = mx - drag_ox;
                    wins[drag_win].y = my - drag_oy;
                    /* Clamp */
                    if (wins[drag_win].x < -wins[drag_win].w + 20)
                        wins[drag_win].x = -wins[drag_win].w + 20;
                    if (wins[drag_win].y < 0) wins[drag_win].y = 0;
                    if (wins[drag_win].x > SCREEN_W - 20)
                        wins[drag_win].x = SCREEN_W - 20;
                    if (wins[drag_win].y > SCREEN_H - TASKBAR_H - TITLEBAR_H)
                        wins[drag_win].y = SCREEN_H - TASKBAR_H - TITLEBAR_H;
                    needs_redraw = 1;
                } else {
                    drag_win = -1;
                }
            }

            /* Paint app: draw on canvas */
            if (focused_idx >= 0 && wins[focused_idx].app == APP_PAINT &&
                (ms->buttons & 0x01) && drag_win < 0) {
                paint_t *p = &wins[focused_idx].paint;
                window_t *w = &wins[focused_idx];
                int canvas_x = w->x + 3;
                int canvas_y = w->y + TITLEBAR_H + 3;
                int canvas_w = w->w - 6;
                int canvas_h = w->h - 24;
                if (canvas_w > PAINT_W) canvas_w = PAINT_W;
                if (canvas_h > PAINT_H) canvas_h = PAINT_H;

                int px = mx - canvas_x;
                int py = my - canvas_y;
                if (px >= 0 && px < canvas_w && py >= 0 && py < canvas_h) {
                    p->canvas[py][px] = p->current_color;
                    needs_redraw = 1;
                }
            }
        }

        /* ── Keyboard handling ────────────────────────────────────────── */
        if (keyboard_has_char()) {
            char ch = keyboard_read_char();
            if (ch) gui_key_event(ch);
        }

        /* ── Render ───────────────────────────────────────────────────── */
        if (needs_redraw) {
            /* Desktop background */
            gfx_clear(C_DESKTOP);

            /* Desktop icons */
            draw_desktop_icons();

            /* Windows (bottom to top) */
            for (int i = 0; i < win_count; i++)
                draw_window(&wins[i], i == focused_idx);

            /* Taskbar */
            draw_taskbar();

            /* Mouse cursor (drawn last, on top of everything) */
            if (ms->ready)
                draw_mouse_cursor(mx, my);

            gfx_flush();
            needs_redraw = 0;
        }

        /* Yield to not burn CPU */
        __asm__ __volatile__("hlt");
    }
}

void gui_tick(void)
{
    tick_counter++;
    /* Redraw every second for clock updates */
    if (tick_counter % 100 == 0)
        needs_redraw = 1;
}

void gui_key_event(char ch)
{
    if (focused_idx < 0) return;

    window_t *w = &wins[focused_idx];

    if (w->app == APP_TERMINAL) {
        term_putchar(&w->term, ch);
        needs_redraw = 1;
    }
}
