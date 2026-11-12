/* kprintf.h -- formatted output for the kernel. */
#ifndef LIB_KPRINTF_H
#define LIB_KPRINTF_H

#include <stdarg.h>
#include <stddef.h>

/* Supports %c %s %d %i %u %o %x %X %p %%, flags '-' and '0', and a width. */
void kprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void kvprintf(const char *fmt, va_list ap);
void ksnprintf(char *buf, size_t size, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

#endif
