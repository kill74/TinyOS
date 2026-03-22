/* timer.h — Programmable Interval Timer (PIT 8253/8254) */
#pragma once
#include <stdint.h>

void init_timer(uint32_t frequency_hz);
uint32_t timer_get_ticks(void);
