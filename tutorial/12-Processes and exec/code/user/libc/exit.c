/* exit.c -- exit() runs cleanups (lesson 20: flush stdio), then _exit(). */
#include <unistd.h>

void exit(int status)
{
    _exit(status);
}
