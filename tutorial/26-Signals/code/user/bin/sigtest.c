/* sigtest.c -- signals seen from a program.
 *
 * 1. A handler runs in the middle of a computation, ~once a second
 *    (alarm), and uses the FPU itself. The computation's result must be
 *    exactly that of "fpu 0", which nothing interrupts.
 * 2. alarm() as a timeout: a read() from the terminal is interrupted
 *    after 3 seconds and fails with EINTR.
 */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

static volatile int alarms;

static int on_alarm(int sig)
{
    volatile double x = 1.0;    /* use the FPU behind the program's back */
    for (int i = 0; i < 1000; i++)
        x = x * 1.5 + 1;
    alarms++;
    signal(sig, on_alarm);      /* handlers are one-shot: re-arm */
    alarm(1);
    return 0;
}

static int on_timeout(int sig)
{
    (void)sig;
    return 0;                   /* just interrupt the read() */
}

int main(void)
{
    signal(SIGALRM, on_alarm);
    alarm(1);
    double sum = 0;
    for (int i = 1; i <= 20000000; i++)
        sum += 1.0 / i;
    alarm(0);
    printf("sum %.10f, interrupted %d times\n", sum, alarms);

    char line[100];
    printf("type a line within 3 seconds: ");
    fflush(stdout);
    signal(SIGALRM, on_timeout);
    alarm(3);
    int n = read(0, line, sizeof(line));
    alarm(0);
    if (n < 0 && errno == EINTR)
        printf("\ntoo late\n");
    else
        printf("got %d bytes\n", n);
    return 0;
}
