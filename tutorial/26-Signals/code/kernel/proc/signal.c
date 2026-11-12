/* signal.c -- signals (V7: sig.c; ssig and kill in sys4.c).
 *
 * A signal is one bit in p->sig. Sending it (psignal) only sets the bit,
 * and wakes the process if it's sleeping. The process acts on the signal
 * itself, on its way back to user mode (signal_deliver, called at the end
 * of trap()):
 *
 *   p->handler[sig] == SIG_DFL   it exits: "killed by signal sig"
 *   p->handler[sig] == SIG_IGN   nothing (never even marked pending)
 *   anything else                it calls that address in user mode
 *
 * A sleeping process that gets a signal wakes up, and its sleep() returns
 * -EINTR: the system call is abandoned, returns EINTR, and the signal is
 * acted on as the process returns to user mode.
 */
#include "proc/signal.h"
#include "proc/proc.h"
#include "sys/syscall.h"
#include "abi/errno.h"

void psignal(struct proc *p, int sig)
{
    if (sig <= 0 || sig >= NSIG || p->handler[sig] == SIG_IGN)
        return;
    p->sig |= 1u << sig;
    if (p->state == SLEEPING)
        p->state = RUNNABLE;        /* its sleep() will return -EINTR */
}

int signal_pending(struct proc *p)
{
    for (int sig = 1; sig < NSIG; sig++)
        if (p->sig & (1u << sig))
            return sig;
    return 0;
}

/* Calling a handler. The kernel can't call user code, so it makes the
 * process do it: it pushes the signal number and the interrupted eflags
 * and eip on the user stack, then "returns" to the handler.
 *
 *   user stack:  ... | eip | eflags | sig |  <- esp when the handler starts
 *
 * The handler is libc's _sigtramp (user/libc/sigtramp.asm). It saves the
 * other registers, calls the C function the program gave signal(), restores
 * everything, and returns to eip: the program continues where it was
 * interrupted. (V7 on the PDP-11 did exactly this, with the processor
 * status word in place of eflags.) */
void signal_deliver(struct trapframe *tf)
{
    struct proc *p = current;
    int sig = signal_pending(p);
    if (!sig)
        return;
    p->sig &= ~(1u << sig);

    uint32_t handler = p->handler[sig];
    if (handler == SIG_DFL)
        proc_exit(sig);             /* V7 also wrote a core file for some */

    /* One-shot, as in V7: the next one gets the default, unless the
     * handler calls signal() again. (Not for SIGILL and SIGTRAP, which
     * debuggers and emulators expect to catch repeatedly.) */
    if (sig != SIGILL && sig != SIGTRAP)
        p->handler[sig] = SIG_DFL;

    uint32_t *sp = (uint32_t *)tf->user_esp - 3;
    if (!user_ok(sp, 3 * sizeof(uint32_t), 1))
        proc_exit(SIGSEGV);         /* no room on the stack: give up */
    sp[0] = sig;
    sp[1] = tf->eflags;
    sp[2] = tf->eip;
    tf->user_esp = (uint32_t)sp;
    tf->eip = handler;
}

/* V7 sends terminal signals to the terminal's "process group": everybody
 * who logged in on it. We have one terminal and no logins: that's every
 * process but init. */
void signal_terminal(int sig)
{
    for (int i = 0; i < NPROC; i++) {
        struct proc *p = &proc_table[i];
        if (p->state != UNUSED && p->state != ZOMBIE && p->pid != 1)
            psignal(p, sig);
    }
}

void alarm_tick(void)
{
    for (int i = 0; i < NPROC; i++) {
        struct proc *p = &proc_table[i];
        if (p->state != UNUSED && p->alarm && --p->alarm == 0)
            psignal(p, SIGALRM);
    }
}

/* ---- system calls ---- */

/* signal(sig, handler): returns the previous handler. */
int32_t sys_signal(struct trapframe *tf)
{
    int sig = tf->ebx;
    uint32_t handler = tf->ecx;

    if (sig <= 0 || sig >= NSIG || sig == SIGKILL)
        return -EINVAL;             /* SIGKILL always kills */
    uint32_t old = current->handler[sig];
    current->handler[sig] = handler;
    current->sig &= ~(1u << sig);   /* forget one already pending (V7) */
    return old;
}

/* kill(pid, sig). pid 0: every process on our terminal (see
 * signal_terminal). pid -1, by root: every process. Never init.
 * Only root may signal another user's processes. sig 0 sends nothing,
 * but says whether pid exists. */
int32_t sys_kill(struct trapframe *tf)
{
    int pid = tf->ebx, sig = tf->ecx;
    int found = 0;

    if (sig < 0 || sig >= NSIG)
        return -EINVAL;
    if (pid == -1 && current->uid != 0)
        return -EPERM;
    for (int i = 0; i < NPROC; i++) {
        struct proc *p = &proc_table[i];
        if (p->state == UNUSED || p->state == ZOMBIE)
            continue;
        if (pid > 0 ? p->pid != pid : p->pid == 1)
            continue;
        if (current->uid != 0 && current->uid != p->uid)
            continue;
        found = 1;
        psignal(p, sig);
    }
    return found ? 0 : -ESRCH;
}

/* alarm(seconds): SIGALRM in that many seconds (0: cancel).
 * Returns the seconds that were left on the previous alarm. */
int32_t sys_alarm(struct trapframe *tf)
{
    uint32_t left = current->alarm;
    current->alarm = tf->ebx;
    return left;
}

/* pause(): sleep until a signal arrives. Always "fails" with EINTR. */
int32_t sys_pause(struct trapframe *tf)
{
    static char nobody_wakes_this;
    (void)tf;
    while (sleep(&nobody_wakes_this) == 0)
        ;
    return -EINTR;
}
