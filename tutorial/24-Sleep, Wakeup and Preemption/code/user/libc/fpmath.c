/* fpmath.c -- frexp, ldexp, modf: the floating-point helpers that V7's
 * atof and ecvt need (V7: PDP-11 assembly). Written with the IEEE 754
 * double layout:
 *
 *   bit 63: sign | bits 62-52: exponent + 1023 | bits 51-0: fraction
 *   value = 1.fraction * 2^(exponent)
 */
#include <math.h>

union bits {
    double d;
    unsigned long long u;
};

#define EXP_SHIFT 52
#define EXP_MASK  0x7FFULL
#define BIAS      1023

/* x = m * 2^e with 0.5 <= |m| < 1. Returns m, sets *e. */
double frexp(double x, int *e)
{
    union bits b = { x };
    int exp = (b.u >> EXP_SHIFT) & EXP_MASK;

    if (x == 0 || exp == EXP_MASK) {       /* zero, infinity, NaN */
        *e = 0;
        return x;
    }
    if (exp == 0) {                         /* denormal: scale up first */
        x = frexp(x * 18014398509481984.0, e);     /* 2^54 */
        *e -= 54;
        return x;
    }
    *e = exp - (BIAS - 1);
    b.u = (b.u & ~(EXP_MASK << EXP_SHIFT)) | ((unsigned long long)(BIAS - 1) << EXP_SHIFT);
    return b.d;
}

/* x * 2^e */
double ldexp(double x, int e)
{
    while (e > 0) {
        x *= 2;
        e--;
    }
    while (e < 0) {
        x /= 2;
        e++;
    }
    return x;
}

/* Split x into an integer part (*ip) and a fraction, both with x's sign. */
double modf(double x, double *ip)
{
    union bits b = { x };
    int exp = (int)((b.u >> EXP_SHIFT) & EXP_MASK) - BIAS;

    if (exp < 0) {                          /* |x| < 1 */
        *ip = 0;
        return x;
    }
    if (exp >= 52) {                        /* no fraction bits left */
        *ip = x;
        return 0;
    }
    b.u &= ~((1ULL << (52 - exp)) - 1);     /* clear the fraction bits */
    *ip = b.d;
    return x - b.d;
}
