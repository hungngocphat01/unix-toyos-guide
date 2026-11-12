/* printf.c -- printf with %c %s %d %u %o %x, flags '-' and '0', and a width.
 * Formats into a buffer, then makes ONE write() system call.
 * Temporary: lesson 20 replaces it with V7's stdio. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

struct out {
    char buf[1024];
    int  len;
};

static void put(struct out *o, char c)
{
    if (o->len < (int)sizeof(o->buf))
        o->buf[o->len++] = c;
}

/* Print sign + s[0..len) padded to width. */
static void field(struct out *o, char sign, const char *s, int len,
                  int width, int left, int zero)
{
    int pad = width - len - (sign ? 1 : 0);
    if (!left && !zero)
        for (; pad > 0; pad--) put(o, ' ');
    if (sign)
        put(o, sign);
    if (!left && zero)
        for (; pad > 0; pad--) put(o, '0');
    while (len--)
        put(o, *s++);
    for (; pad > 0; pad--)
        put(o, ' ');
}

static void number(struct out *o, unsigned n, unsigned base, char sign,
                   int width, int left, int zero)
{
    char digits[16];
    int i = sizeof(digits);
    do {
        digits[--i] = "0123456789abcdef"[n % base];
        n /= base;
    } while (n);
    field(o, sign, digits + i, sizeof(digits) - i, width, left, zero);
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
        int left = 0, zero = 0, width = 0;
        for (fmt++; *fmt == '-' || *fmt == '0'; fmt++)
            *fmt == '-' ? (left = 1) : (zero = 1);
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + *fmt++ - '0';
        if (*fmt == 'l')
            fmt++;

        switch (*fmt) {
        case 'd': {
            int v = va_arg(ap, int);
            number(&o, v < 0 ? -(unsigned)v : (unsigned)v, 10, v < 0 ? '-' : 0,
                   width, left, zero);
            break;
        }
        case 'u': number(&o, va_arg(ap, unsigned), 10, 0, width, left, zero); break;
        case 'o': number(&o, va_arg(ap, unsigned), 8, 0, width, left, zero); break;
        case 'x': number(&o, va_arg(ap, unsigned), 16, 0, width, left, zero); break;
        case 'c': {
            char c = (char)va_arg(ap, int);
            field(&o, 0, &c, 1, width, left, 0);
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            field(&o, 0, s, strlen(s), width, left, 0);
            break;
        }
        case '%': put(&o, '%'); break;
        case '\0': fmt--; break;
        }
    }
    va_end(ap);
    return write(1, o.buf, o.len);
}
