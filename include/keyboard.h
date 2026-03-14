/* keyboard.h — PS/2 keyboard driver */
#pragma once
#include <stdint.h>

void init_keyboard(void);

/* Returns the last ASCII character pressed (0 if none yet). */
char keyboard_last_char(void);
