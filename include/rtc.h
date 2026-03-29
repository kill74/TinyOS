/* rtc.h — CMOS Real-Time Clock */
#pragma once
#include <stdint.h>

typedef struct {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint16_t year;
} rtc_time_t;

/* Read the current time from the CMOS RTC. */
void rtc_read(rtc_time_t *t);

/* Get a compact "HH:MM:SS" string from the RTC. */
void rtc_time_str(char *buf);
