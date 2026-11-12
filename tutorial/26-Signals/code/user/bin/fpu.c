/* fpu.c -- a long floating-point computation, to test that the kernel
 * saves each process's FPU registers when it switches.
 *
 *   fpu 1 & fpu 2 & wait      must print the same numbers as
 *   fpu 1; fpu 2              (one at a time)
 *
 * The running sum stays in an x87 register for the whole loop, so a
 * switch that didn't save the FPU would hand it to the other process. */
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    int k = argc > 1 ? atoi(argv[1]) : 1;
    double sum = 0;

    for (int i = 1; i <= 20000000; i++)
        sum += 1.0 / (i + k);
    printf("fpu %d: %.10f\n", k, sum);
    return 0;
}
