/* rtc.h -- the CMOS real-time clock: what time is it? */
#ifndef DEV_RTC_H
#define DEV_RTC_H

#include <stdint.h>

void     rtc_init(void);        /* read the date and time once, at boot */
uint32_t time_now(void);        /* seconds since 1970-01-01 00:00 UTC */
void     time_set(uint32_t t);  /* stime() */

#endif
