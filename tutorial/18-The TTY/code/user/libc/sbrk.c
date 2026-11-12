/* sbrk.c -- grow the heap. Same logic as V7's libc/sys/sbrk.s. */
#include <unistd.h>

extern char end[];              /* user.ld: end of the program */
static char *curbrk = end;      /* current end of the heap */

void *sbrk(int incr)
{
    char *old = curbrk;
    if (brk(curbrk + incr) < 0)
        return (void *)-1;
    curbrk += incr;
    return old;
}
