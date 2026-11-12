/* sbrk.c -- grow the heap (malloc calls it). Same logic as V7's sbrk.s. */
#include <unistd.h>

extern char end[];              /* user.ld: end of the program */
static char *curbrk = end;      /* current end of the heap */

char *sbrk(int incr)
{
    char *old = curbrk;
    if (brk(curbrk + incr) < 0)
        return (char *)-1;
    curbrk += incr;
    return old;
}
