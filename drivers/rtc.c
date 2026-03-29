/* rtc.c — CMOS Real-Time Clock driver
 *
 * The RTC lives in the CMOS chip, accessed via ports 0x70 (index) and
 * 0x71 (data). Register 0x0A bit 7 tells us when an update is in
 * progress; we spin until it clears before reading.
 *
 * Values may be BCD or binary depending on register 0x0B bit 2.
 */

#include "../include/rtc.h"
#include <stdint.h>

extern void outb(uint16_t port, uint8_t value);
extern uint8_t inb(uint16_t port);

#define CMOS_INDEX 0x70
#define CMOS_DATA  0x71

static uint8_t cmos_read(uint8_t reg)
{
    outb(CMOS_INDEX, reg);
    return inb(CMOS_DATA);
}

static int rtc_updating(void)
{
    outb(CMOS_INDEX, 0x0A);
    return (inb(CMOS_DATA) & 0x80) != 0;
}

static uint8_t bcd_to_bin(uint8_t bcd)
{
    return (uint8_t)((bcd & 0x0F) + ((bcd >> 4) * 10));
}

void rtc_read(rtc_time_t *t)
{
    /* Wait for any in-progress update to finish */
    while (rtc_updating())
        ;

    t->second = cmos_read(0x00);
    t->minute = cmos_read(0x02);
    t->hour   = cmos_read(0x04);
    t->day    = cmos_read(0x07);
    t->month  = cmos_read(0x08);
    t->year   = cmos_read(0x09);

    /* Check if values are BCD (bit 2 of register 0x0B = 0 means BCD) */
    uint8_t status_b = cmos_read(0x0B);
    if (!(status_b & 0x04)) {
        t->second = bcd_to_bin(t->second);
        t->minute = bcd_to_bin(t->minute);
        t->hour   = bcd_to_bin(t->hour & 0x7F);
        t->day    = bcd_to_bin(t->day);
        t->month  = bcd_to_bin(t->month);
        t->year   = bcd_to_bin(t->year);
    }

    /* Convert 12-hour to 24-hour if needed */
    if (!(status_b & 0x02) && (cmos_read(0x04) & 0x80)) {
        t->hour = (uint8_t)((t->hour + 12) % 24);
    }

    /* Year is 2-digit; assume 2000+ */
    t->year = (uint16_t)(2000 + t->year);
}

void rtc_time_str(char *buf)
{
    rtc_time_t t;
    rtc_read(&t);

    buf[0] = '0' + (t.hour / 10);
    buf[1] = '0' + (t.hour % 10);
    buf[2] = ':';
    buf[3] = '0' + (t.minute / 10);
    buf[4] = '0' + (t.minute % 10);
    buf[5] = ':';
    buf[6] = '0' + (t.second / 10);
    buf[7] = '0' + (t.second % 10);
    buf[8] = '\0';
}
