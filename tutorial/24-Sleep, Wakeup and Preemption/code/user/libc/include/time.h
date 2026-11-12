/* time.h -- V7's struct tm, plus declarations. */
#ifndef _TIME_H
#define _TIME_H

#include <sys/cdefs.h>
#include <sys/types.h>

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;                 /* 0..11 */
    int tm_year;                /* years since 1900 */
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

time_t     time __P((time_t *));
int        stime __P((time_t *));           /* set the clock: root only */
char      *ctime __P((const time_t *));
struct tm *localtime __P((const time_t *));
struct tm *gmtime __P((const time_t *));
char      *asctime __P((const struct tm *));

#endif
