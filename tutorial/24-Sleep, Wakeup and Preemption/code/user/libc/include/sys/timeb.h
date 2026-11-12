/* sys/timeb.h -- for ftime(), which ctime uses to learn the time zone. */
#ifndef _SYS_TIMEB_H
#define _SYS_TIMEB_H

#include <sys/cdefs.h>
#include <sys/types.h>

struct timeb {
    time_t         time;
    unsigned short millitm;
    short          timezone;    /* minutes west of Greenwich */
    short          dstflag;
};

int ftime __P((struct timeb *));

#endif
