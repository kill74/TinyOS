/* font.h — 8x8 bitmap font for GUI text rendering */
#pragma once
#include <stdint.h>

/* Font dimensions */
#define FONT_W  8
#define FONT_H  8

/* Get the bitmap data for a character (8 bytes, one per row) */
const uint8_t *font_get_glyph(char c);

/* Draw a string at pixel coordinates with fg/bg colours */
void font_draw_string(int x, int y, const char *s, uint8_t fg, uint8_t bg);

/* Draw a single character */
void font_draw_char(int x, int y, char c, uint8_t fg, uint8_t bg);
