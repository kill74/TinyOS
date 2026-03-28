/* mouse.h — PS/2 mouse driver */
#pragma once
#include <stdint.h>

#define MOUSE_PACKET_SIZE 3

/* Mouse state */
typedef struct {
    int x, y;
    uint8_t buttons;   /* bit 0 = left, bit 1 = right, bit 2 = middle */
    uint8_t ready;     /* set to 1 after first packet received */
} mouse_state_t;

/* Initialise PS/2 mouse (enable IRQ12, set stream mode) */
void mouse_init(void);

/* Get current mouse state */
const mouse_state_t *mouse_get(void);

/* Check if left button was just pressed this frame */
int mouse_left_clicked(void);

/* Check if left button is held */
int mouse_left_held(void);
