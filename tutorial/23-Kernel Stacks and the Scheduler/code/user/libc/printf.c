/* printf.c -- printf, fprintf, sprintf: all three call _doprnt.
 * (V7 passed &args, the address of the first variable argument, which
 * only works where arguments are always on the stack. stdarg.h is the
 * portable way.) */
#include <stdarg.h>
#include <stdio.h>

int _doprnt(const char *fmt, va_list ap, FILE *out);

int printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = _doprnt(fmt, ap, stdout);
    va_end(ap);
    return r;
}

int fprintf(FILE *out, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = _doprnt(fmt, ap, out);
    va_end(ap);
    return r;
}

/* sprintf: a fake FILE whose "buffer" is the caller's string, with
 * _IOSTRG so that nothing ever flushes it (V7's trick). */
char *sprintf(char *str, const char *fmt, ...)
{
    FILE strbuf;
    va_list ap;

    strbuf._flag = _IOWRT | _IOSTRG;
    strbuf._ptr = str;
    strbuf._cnt = 32767;
    va_start(ap, fmt);
    _doprnt(fmt, ap, &strbuf);
    va_end(ap);
    putc('\0', &strbuf);
    return str;
}
