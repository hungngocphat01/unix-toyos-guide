/* math.h -- the few functions V7's libc itself needs. */
#ifndef _MATH_H
#define _MATH_H

#include <sys/cdefs.h>

/* The largest double, and log10 of it. (V7's values, 1.7e38 and 39, were
 * the PDP-11's float limits; these are IEEE 754 double's.) */
#define HUGE    1.7976931348623157e308
#define LOGHUGE 308

double frexp __P((double, int *));
double ldexp __P((double, int));
double modf __P((double, double *));
double atof __P((const char *));

#endif
