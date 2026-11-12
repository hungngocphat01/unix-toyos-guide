/* sleep.c -- wait n seconds. A stand-in: it watches the clock, using the
 * CPU the whole time. Lesson 26 replaces it with V7's (alarm + pause). */
#include <time.h>

unsigned sleep(unsigned n)
{
    time_t end = time(0) + n;
    while (time(0) < end)
        ;
    return 0;
}
