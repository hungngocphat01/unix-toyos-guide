/* ftime.c -- V7 system call 35, which ctime() uses to learn the time zone.
 * We have no time zones: everything is UTC, so it's built on time(). */
#include <time.h>
#include <sys/timeb.h>

int ftime(struct timeb *tp)
{
    tp->time = time(0);
    tp->millitm = 0;
    tp->timezone = 0;           /* minutes west of Greenwich */
    tp->dstflag = 0;
    return 0;
}
