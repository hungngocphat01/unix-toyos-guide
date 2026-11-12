/* printf.c -- a minimal printf: %d %u %x %s %c %%, no width or flags.
 * Formats into a buffer, then makes ONE write() system call.
 * Temporary: lesson 20 replaces it with V7's stdio. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

struct out {
    char buf[512];
    int  len;
};

static void put(struct out *o, char c)
{
    if (o->len < (int)sizeof(o->buf))
        o->buf[o->len++] = c;
}

static void put_num(struct out *o, unsigned n, unsigned base)
{
    char digits[16];
    int i = 0;
    do {
        digits[i++] = "0123456789abcdef"[n % base];
        n /= base;
    } while (n);
    while (i > 0)
        put(o, digits[--i]);
}

int printf(const char *fmt, ...)
{
    struct out o;
    va_list ap;

    o.len = 0;
    va_start(ap, fmt);
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            put(&o, *fmt);
            continue;
        }
        switch (*++fmt) {
        case 'd': {
            int v = va_arg(ap, int);
            if (v < 0) {
                put(&o, '-');
                put_num(&o, -(unsigned)v, 10);
            } else {
                put_num(&o, v, 10);
            }
            break;
        }
        case 'u': put_num(&o, va_arg(ap, unsigned), 10); break;
        case 'x': put_num(&o, va_arg(ap, unsigned), 16); break;
        case 'c': put(&o, (char)va_arg(ap, int)); break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            while (*s)
                put(&o, *s++);
            break;
        }
        case '%': put(&o, '%'); break;
        case '\0': fmt--; break;
        }
    }
    va_end(ap);
    return write(1, o.buf, o.len);
}
