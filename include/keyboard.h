/* keyboard.h — PS/2 keyboard driver */
#pragma once
#include <stdint.h>

void init_keyboard(void);

/* Returns the last ASCII character pressed (0 if none yet). */
char keyboard_last_char(void);

/* Returns true if a character is available in the keyboard buffer. */
int keyboard_has_char(void);

/* Reads the next character from the keyboard buffer (non-blocking). */
char keyboard_read_char(void);
