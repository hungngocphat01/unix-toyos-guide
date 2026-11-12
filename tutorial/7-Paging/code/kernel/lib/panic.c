/* panic.c -- print a message and stop the machine. */
#include <stdarg.h>
#include "lib/panic.h"
#include "lib/kprintf.h"

void panic(const char *fmt, ...)
{
    va_list ap;

    __asm__ volatile ("cli");           /* nothing may interrupt us now */
    kprintf("\nPANIC: ");
    va_start(ap, fmt);
    kvprintf(fmt, ap);
    va_end(ap);
    kprintf("\n");

    for (;;)
        __asm__ volatile ("hlt");
}
