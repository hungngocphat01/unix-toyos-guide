/* doprnt.c -- the engine behind printf, fprintf and sprintf.
 * (V7's _doprnt is PDP-11 assembly: doprnt.s and fltpr.s.)
 *
 * Conversions, as in V7: %d %u %o %x %c %s %e %f %g %%.
 * Flags: '-' (left-justify), '0' (pad with zeros); a width and a
 * precision, either of which may be '*'. 'l' or a capital letter (%D %O
 * %X %U) means long, which is the same as int on i386.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

char *ecvt(), *fcvt(), *gcvt();     /* v7/ecvt.c, v7/gcvt.c */

/* Print s (len bytes), with an optional sign/prefix, padded to width. */
static void field(FILE *out, const char *prefix, const char *s, int len,
                  int width, int left, int zero)
{
    int plen = strlen(prefix);
    int pad = width - plen - len;

    if (!left && !zero)
        for (; pad > 0; pad--) putc(' ', out);
    while (*prefix)
        putc(*prefix++, out);
    if (!left && zero)
        for (; pad > 0; pad--) putc('0', out);
    while (len-- > 0)
        putc(*s++, out);
    for (; pad > 0; pad--)
        putc(' ', out);
}

/* Digits of n in base, into the end of buf. Returns the first digit. */
static char *digits(unsigned long n, int base, char *end)
{
    char *p = end;
    do {
        *--p = "0123456789abcdef"[n % base];
        n /= base;
    } while (n);
    return p;
}

/* %e, %f, %g of |x| with prec digits after the point (%g: prec
 * significant digits), into buf. Returns 1 if x is negative. */
static int fmt_float(char *buf, double x, int conv, int prec)
{
    int decpt, sign;
    char *p = buf, *d;

    if (conv == 'g') {                  /* shortest form: V7's gcvt */
        gcvt(x < 0 ? -x : x, prec ? prec : 1, buf);
        return x < 0;
    }
    if (conv == 'e') {                  /* d.ddde+xx */
        d = ecvt(x, prec + 1, &decpt, &sign);
        *p++ = *d ? *d++ : '0';
        if (prec > 0)
            *p++ = '.';
        while (prec-- > 0)
            *p++ = *d ? *d++ : '0';
        int e = x == 0 ? 0 : decpt - 1;
        *p++ = 'e';
        *p++ = e < 0 ? '-' : '+';
        if (e < 0)
            e = -e;
        *p++ = '0' + e / 10 % 10;
        *p++ = '0' + e % 10;
    } else {                            /* ddd.ddd */
        d = fcvt(x, prec, &decpt, &sign);
        if (decpt <= 0)
            *p++ = '0';
        for (; decpt > 0; decpt--)
            *p++ = *d ? *d++ : '0';
        if (prec > 0)
            *p++ = '.';
        for (; prec > 0; prec--)
            *p++ = decpt++ < 0 ? '0' : (*d ? *d++ : '0');
    }
    *p = '\0';
    return sign;
}

int _doprnt(const char *fmt, va_list ap, FILE *out)
{
    char buf[400];                      /* a number, or a float's digits */
    char *s;

    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            putc(*fmt, out);
            continue;
        }

        int left = 0, zero = 0, width = 0, prec = -1, is_long = 0;
        for (fmt++; *fmt == '-' || *fmt == '0'; fmt++)
            *fmt == '-' ? (left = 1) : (zero = 1);
        if (*fmt == '*') {
            width = va_arg(ap, int);
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + *fmt++ - '0';
        if (*fmt == '.') {
            prec = 0;
            if (*++fmt == '*') {
                prec = va_arg(ap, int);
                fmt++;
            }
            while (*fmt >= '0' && *fmt <= '9')
                prec = prec * 10 + *fmt++ - '0';
        }
        if (*fmt == 'l') {
            is_long = 1;
            fmt++;
        }
        (void)is_long;                  /* long == int on i386 */

        int conv = *fmt;
        if (conv >= 'A' && conv <= 'Z' && conv != 'X')
            conv += 'a' - 'A';          /* %D = %ld etc. */
        if (conv == 'X')
            conv = 'x';

        switch (conv) {
        case 'd': {
            long v = va_arg(ap, long);
            s = digits(v < 0 ? -(unsigned long)v : (unsigned long)v, 10, buf + sizeof(buf));
            field(out, v < 0 ? "-" : "", s, buf + sizeof(buf) - s, width, left, zero);
            break;
        }
        case 'u':
        case 'o':
        case 'x':
            s = digits(va_arg(ap, unsigned long), conv == 'u' ? 10 : conv == 'o' ? 8 : 16,
                       buf + sizeof(buf));
            field(out, "", s, buf + sizeof(buf) - s, width, left, zero);
            break;
        case 'c':
            buf[0] = va_arg(ap, int);
            field(out, "", buf, 1, width, left, 0);
            break;
        case 's': {
            s = va_arg(ap, char *);
            if (!s)
                s = "(null)";
            int len = strlen(s);
            if (prec >= 0 && prec < len)
                len = prec;
            field(out, "", s, len, width, left, 0);
            break;
        }
        case 'e':
        case 'f':
        case 'g': {
            int sign = fmt_float(buf, va_arg(ap, double), conv, prec < 0 ? 6 : prec);
            field(out, sign ? "-" : "", buf, strlen(buf), width, left, zero);
            break;
        }
        case '\0':
            return 0;
        default:                        /* %% and unknown conversions */
            putc(*fmt, out);
            break;
        }
    }
    return ferror(out) ? EOF : 0;
}
