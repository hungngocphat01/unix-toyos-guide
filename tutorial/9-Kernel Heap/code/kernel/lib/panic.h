/* panic.h -- give up loudly. */
#ifndef LIB_PANIC_H
#define LIB_PANIC_H

void panic(const char *fmt, ...)
    __attribute__((noreturn, format(printf, 1, 2)));

#define assert(e) \
    ((e) ? (void)0 : panic("assert(%s) failed at %s:%d", #e, __FILE__, __LINE__))

#endif
