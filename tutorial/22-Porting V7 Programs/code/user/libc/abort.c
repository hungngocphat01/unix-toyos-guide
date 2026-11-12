/* abort.c -- die with a "core dump" signal. V7 executes the PDP-11 iot
 * instruction (SIGIOT); we execute ud2, an instruction guaranteed to be
 * invalid, and the kernel kills us with SIGILL. */
#include <stdlib.h>

void abort(void)
{
    for (;;)
        __asm__ volatile ("ud2");
}
