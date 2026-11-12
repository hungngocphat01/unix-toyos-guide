/* scanf.c -- scanf, fscanf, sscanf: all three call V7's _doscan
 * (v7/doscan.c), which walks the arguments as an array of pointers
 * (int **). On i386 variable arguments are 4-byte stack slots and every
 * scanf argument is a pointer, so a va_list IS such an array. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

int _doscan();

int scanf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = _doscan(stdin, fmt, (int **)ap);
    va_end(ap);
    return r;
}

int fscanf(FILE *in, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = _doscan(in, fmt, (int **)ap);
    va_end(ap);
    return r;
}

/* sscanf: a fake FILE reading from the string (V7's trick again). */
int sscanf(const char *str, const char *fmt, ...)
{
    FILE strbuf;
    va_list ap;

    strbuf._flag = _IOREAD | _IOSTRG;
    strbuf._ptr = strbuf._base = (char *)str;
    strbuf._cnt = strlen(str);
    va_start(ap, fmt);
    int r = _doscan(&strbuf, fmt, (int **)ap);
    va_end(ap);
    return r;
}
