/* signal.h -- signals, kernel side. Numbers in abi/signal.h. */
#ifndef PROC_SIGNAL_H
#define PROC_SIGNAL_H

#include "arch/i386/trap.h"
#include "abi/signal.h"

struct proc;

/* What a process does with a signal (p->handler[sig]): */
#define SIG_DFL 0                   /* the default: exit */
#define SIG_IGN 1                   /* ignore it */
                                    /* anything else: a user address to call */

/* Send sig to p: mark it pending and wake p if it sleeps. Safe in
 * interrupt handlers. */
void psignal(struct proc *p, int sig);

/* The signal p should act on now, or 0. */
int signal_pending(struct proc *p);

/* On the way back to user mode: act on a pending signal. Either exit, or
 * rewrite tf so that the process "calls" its handler. */
void signal_deliver(struct trapframe *tf);

/* ^C and ^\ on the terminal: signal every process using it. */
void signal_terminal(int sig);

/* Once a second, from the clock: count down alarms, send SIGALRM. */
void alarm_tick(void);

#endif
