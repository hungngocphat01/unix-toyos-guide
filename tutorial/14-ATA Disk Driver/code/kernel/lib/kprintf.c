/* kprintf.c -- printf-style formatting.
 *
 * The core, format(), knows nothing about devices: it hands every output
 * character to a callback. kprintf() passes a callback that prints to the
 * console; ksnprintf() passes one that stores into a buffer.
 */
#include <stdint.h>
#include "lib/kprintf.h"
#include "lib/string.h"
#include "dev/console.h"

typedef void (*putc_fn)(char c, void *ctx);

struct spec {
    int width;      /* minimum field width             */
    int left;       /* '-' flag: pad on the right      */
    int zero;       /* '0' flag: pad with zeros        */
};

/* Print [sign] + s[0..len) padded to sp.width. */
static void emit(putc_fn out, void *ctx, char sign, const char *s, int len,
                 struct spec sp)
{
    int pad = sp.width - len - (sign ? 1 : 0);

    if (!sp.left && !sp.zero)
        for (; pad > 0; pad--) out(' ', ctx);
    if (sign)
        out(sign, ctx);
    if (!sp.left && sp.zero)
        for (; pad > 0; pad--) out('0', ctx);
    for (int i = 0; i < len; i++)
        out(s[i], ctx);
    for (; pad > 0; pad--)                  /* only left-aligned is left */
        out(' ', ctx);
}

static void emit_num(putc_fn out, void *ctx, uint32_t n, unsigned base,
                     int upper, char sign, struct spec sp)
{
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char buf[32];
    int i = sizeof buf;

    do {                                    /* least significant digit first */
        buf[--i] = digits[n % base];
        n /= base;
    } while (n);
    emit(out, ctx, sign, buf + i, sizeof buf - i, sp);
}

static void format(putc_fn out, void *ctx, const char *fmt, va_list ap)
{
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            out(*fmt, ctx);
            continue;
        }
        fmt++;

        struct spec sp = {0, 0, 0};
        for (;; fmt++) {
            if (*fmt == '-')      sp.left = 1;
            else if (*fmt == '0') sp.zero = 1;
            else break;
        }
        while (*fmt >= '0' && *fmt <= '9')
            sp.width = sp.width * 10 + (*fmt++ - '0');
        while (*fmt == 'l')                 /* long is the same as int on i386 */
            fmt++;

        switch (*fmt) {
        case 'c': {
            char c = (char)va_arg(ap, int);  /* char is promoted to int */
            emit(out, ctx, 0, &c, 1, sp);
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s)
                s = "(null)";
            emit(out, ctx, 0, s, strlen(s), sp);
            break;
        }
        case 'd':
        case 'i': {
            int v = va_arg(ap, int);
            /* Negate as unsigned: -INT_MIN does not fit in an int. */
            uint32_t mag = v < 0 ? -(uint32_t)v : (uint32_t)v;
            emit_num(out, ctx, mag, 10, 0, v < 0 ? '-' : 0, sp);
            break;
        }
        case 'u': emit_num(out, ctx, va_arg(ap, unsigned), 10, 0, 0, sp); break;
        case 'x': emit_num(out, ctx, va_arg(ap, unsigned), 16, 0, 0, sp); break;
        case 'X': emit_num(out, ctx, va_arg(ap, unsigned), 16, 1, 0, sp); break;
        case 'p':
            out('0', ctx);
            out('x', ctx);
            sp.width = 8;
            sp.zero = 1;
            emit_num(out, ctx, (uintptr_t)va_arg(ap, void *), 16, 0, 0, sp);
            break;
        case '%':
            out('%', ctx);
            break;
        case '\0':                          /* format ended with a lone '%' */
            return;
        default:                            /* unknown: print it as-is */
            out('%', ctx);
            out(*fmt, ctx);
            break;
        }
    }
}

/* ---- kprintf: output to the console ---- */

static void console_out(char c, void *ctx)
{
    (void)ctx;
    console_putc(c);
}

void kvprintf(const char *fmt, va_list ap)
{
    format(console_out, NULL, fmt, ap);
}

void kprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    kvprintf(fmt, ap);
    va_end(ap);
}

/* ---- ksnprintf: output to a buffer, always NUL-terminated ---- */

struct bufout {
    char  *buf;
    size_t size;
    size_t pos;
};

static void buffer_out(char c, void *ctx)
{
    struct bufout *b = ctx;
    if (b->pos + 1 < b->size)
        b->buf[b->pos++] = c;
}

void ksnprintf(char *buf, size_t size, const char *fmt, ...)
{
    struct bufout b = { buf, size, 0 };
    va_list ap;

    va_start(ap, fmt);
    format(buffer_out, &b, fmt, ap);
    va_end(ap);
    if (size > 0)
        buf[b.pos] = '\0';
}
