/* signal.c -- a stand-in until lesson 26 implements signals. Programs
 * call signal() to ignore or catch ^C; since no signal is ever delivered
 * yet, remembering nothing and answering "it was the default" is enough. */
#include <signal.h>

int (*signal(int sig, int (*handler)()))()
{
    (void)sig;
    (void)handler;
    return SIG_DFL;
}
