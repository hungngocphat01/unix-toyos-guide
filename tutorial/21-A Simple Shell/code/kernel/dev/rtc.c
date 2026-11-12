/* rtc.c -- read the date from the CMOS clock at boot; after that, count
 * timer ticks. (QEMU's CMOS clock runs in UTC.) */
#include "dev/rtc.h"
#include "arch/i386/io.h"
#include "arch/i386/pit.h"

#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71

/* CMOS registers */
#define SECONDS 0x00
#define MINUTES 0x02
#define HOURS   0x04
#define DAY     0x07
#define MONTH   0x08
#define YEAR    0x09
#define STATUS_A 0x0A           /* bit 7: an update is in progress */
#define STATUS_B 0x0B           /* bit 2: binary (else BCD); bit 1: 24-hour */

static uint32_t boot_time;

static uint8_t cmos(uint8_t reg)
{
    outb(CMOS_ADDR, reg);
    return inb(CMOS_DATA);
}

/* Days from 1970-01-01 to the first day of `year` and `month`. */
static uint32_t days_before(int year, int month)
{
    static const int month_days[12] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
    uint32_t days = 0;
    for (int y = 1970; y < year; y++)
        days += (y % 4 == 0) ? 366 : 365;           /* right for 1901..2099 */
    days += month_days[month - 1];
    if (month > 2 && year % 4 == 0)
        days++;
    return days;
}

void rtc_init(void)
{
    while (cmos(STATUS_A) & 0x80)
        ;                       /* wait for the end of an update */

    int sec = cmos(SECONDS), min = cmos(MINUTES), hour = cmos(HOURS);
    int day = cmos(DAY), month = cmos(MONTH), year = cmos(YEAR);

    if (!(cmos(STATUS_B) & 0x04)) {                 /* BCD: 0x59 means 59 */
#define BCD(x) (((x) >> 4) * 10 + ((x) & 0x0F))
        sec = BCD(sec); min = BCD(min); hour = BCD(hour);
        day = BCD(day); month = BCD(month); year = BCD(year);
    }
    year += 2000;

    boot_time = ((days_before(year, month) + day - 1) * 24 + hour) * 3600
                + min * 60 + sec;
}

uint32_t time_now(void)
{
    return boot_time + pit_ticks() / HZ;
}
