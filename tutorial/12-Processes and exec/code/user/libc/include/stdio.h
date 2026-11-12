/* stdio.h -- for now only printf. Lesson 20 brings V7's real stdio. */
#ifndef _STDIO_H
#define _STDIO_H

int printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif
