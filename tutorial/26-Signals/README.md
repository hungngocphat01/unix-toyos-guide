# 26 — Signals

**Goal:** ^C works. A **signal** is a software interrupt for a process:
the terminal sends one when you press ^C, the kernel sends one when a
program faults or writes to a dead pipe, a process sends one with `kill`,
the clock sends one after `alarm`. The process either dies (the default),
ignores it, or **catches** it: the kernel makes it call a function, in
the middle of whatever it was doing, and then continue as if nothing
happened. Four system calls (`signal kill alarm pause`) and ~150 lines.

**Milestone:**

```
$ yes > /dev/null
                                    <- ^C: the foreground command dies
$ sleep 100 &                       <- V7's sleep: alarm + pause, no CPU
5
$ ed /etc/motd
18
                                    <- ^C: ed catches it
?
q
$ date; sleep 2; date
Wed Nov 11 15:19:29 GMT 2026
Wed Nov 11 15:19:31 GMT 2026
$ sleep 5
Quit                                <- ^\
$ kill 5; wait                      <- the background sleep
Terminated
$ kill 0                            <- every job (kill too); the shell ignores SIGTERM
Terminated
$ sigtest
sum 17.3884585214, interrupted 2 times     <- = "fpu 0": handlers are invisible
type a line within 3 seconds:
too late                            <- alarm() interrupted read(): EINTR
```

**Reference code:** [`code/`](code/). New:
[`proc/signal.c`](code/kernel/proc/signal.c) and
[`signal.h`](code/kernel/proc/signal.h),
[`user/libc/sigtramp.asm`](code/user/libc/sigtramp.asm), V7's
[`sleep.c`](code/user/libc/v7/sleep.c) and
[`kill.c`](code/user/v7bin/kill.c),
[`user/bin/sigtest.c`](code/user/bin/sigtest.c). Changed:
[`proc/proc.c`](code/kernel/proc/proc.c) (`sleep` returns `-EINTR`),
[`arch/i386/trap.c`](code/kernel/arch/i386/trap.c) (faults become
signals; delivery on the way out), [`dev/tty.c`](code/kernel/dev/tty.c)
(^C, ^\\), [`fs/pipe.c`](code/kernel/fs/pipe.c),
[`proc/exec.c`](code/kernel/proc/exec.c),
[`sys/sys_file.c`](code/kernel/sys/sys_file.c) (SIGPIPE),
[`user/libc/syscall.c`](code/user/libc/syscall.c) (`signal kill alarm
pause`), [`user/bin/sh.c`](code/user/bin/sh.c). Removed: the stand-in
`signal.c` and `sleep.c`. Fixed: `itrunc` erased device numbers (§8).

---

## 1. The model

Each process has, for each signal, a **disposition**
([`proc.h`](code/kernel/proc/proc.h)):

```c
uint32_t sig;               /* pending: bit n set = signal n */
uint32_t handler[NSIG];     /* SIG_DFL (0), SIG_IGN (1), or a user address */
uint32_t alarm;             /* seconds until SIGALRM */
```

Sending and acting are **separate**, in V7 as here:

1. **Send** (`psignal`): set the bit; if the process sleeps, wake it.
   That's all, so it's safe anywhere, even in an interrupt handler (^C
   arrives in the keyboard interrupt).
2. **Act** (`signal_deliver`): the process itself, when it's about to
   return to user mode, looks at its pending bits.

```c
void psignal(struct proc *p, int sig)
{
    if (sig <= 0 || sig >= NSIG || p->handler[sig] == SIG_IGN)
        return;
    p->sig |= 1u << sig;
    if (p->state == SLEEPING)
        p->state = RUNNABLE;        /* its sleep() will return -EINTR */
}
```

Every return to user mode goes through the end of `trap()`, so that's
where signals are acted on:

```c
void trap(struct trapframe *tf)
{
    ... interrupts, system calls, faults ...

    /* Back to user mode: first, act on any signal that arrived. */
    if (from_user(tf))
        signal_deliver(tf);
}
```

A process computing in user mode gets there at the next timer interrupt
(≤ 10 ms). A process in a system call gets there when the call returns.
A process **sleeping** in a system call is woken by `psignal`, and...

## 2. Interrupted system calls: EINTR

...its `sleep()` returns **`-EINTR`** instead of 0
([`proc.c`](code/kernel/proc/proc.c)):

```c
int sleep(void *chan)
{
    if (signal_pending(current))
        return -EINTR;              /* don't even start */
    current->chan = chan;
    current->state = SLEEPING;
    sched();
    current->chan = NULL;
    return signal_pending(current) ? -EINTR : 0;
}
```

and every caller gives up: `tty_read`, `pipe_read`, `pipe_write`, `wait`,
`pause` return `-EINTR`, the system call fails with `EINTR`, and on the
way out the signal is delivered. That's how ^C stops a `cat` waiting for
the keyboard, and how `alarm` implements a timeout on `read()`.

V7 did this with a `longjmp` out of `sleep()` straight back to the system
call entry (`u.u_qsav`). We return an error code instead, like xv6: a bit
more code at each caller, but you can follow the path with your eyes.

> **Only some sleeps should be interruptible.** V7 distinguishes sleeps
> "above PZERO" (waiting for a person: terminal, pipe, child) from sleeps
> below (waiting for the disk: short, and abandoning them would leave a
> buffer half-done). Our disk doesn't sleep at all (lesson 24), so every
> sleep we have is of the first kind.

## 3. Acting on a signal

```c
void signal_deliver(struct trapframe *tf)
{
    int sig = signal_pending(p);
    if (!sig)
        return;
    p->sig &= ~(1u << sig);

    uint32_t handler = p->handler[sig];
    if (handler == SIG_DFL)
        proc_exit(sig);             /* wait() status: "killed by sig" */

    if (sig != SIGILL && sig != SIGTRAP)
        p->handler[sig] = SIG_DFL;  /* one-shot, as in V7 */

    uint32_t *sp = (uint32_t *)tf->user_esp - 3;   /* push on the USER stack: */
    sp[0] = sig;                    /*   the argument        */
    sp[1] = tf->eflags;             /*   the interrupted flags */
    sp[2] = tf->eip;                /*   where it was        */
    tf->user_esp = (uint32_t)sp;
    tf->eip = handler;              /* "return" into the handler */
}
```

(The real code also checks with `user_ok` that the stack can take it.)

**Every** default action in V7 is "exit". There's no SIGCHLD, no stop and
continue: those came with BSD job control. V7 also wrote a `core` file
for the "bug" signals (SIGQUIT, SIGILL, SIGSEGV...) and added `0200` to
the status. We don't write core files.

**One-shot handlers**: after delivery the disposition goes back to
`SIG_DFL`. A handler that wants the next one too calls `signal()` again,
first thing. There's a window where a second ^C kills the program: a
famous V7 wart, fixed by BSD's "reliable signals" and POSIX `sigaction`.

## 4. Calling a handler: the trampoline

The kernel can't call a user function: it can only return to user mode
with some registers. The trick is to make the process look like it
**was interrupted and called the handler itself**. The interrupted `eip`
and `eflags` go on the user stack, and `eip` becomes the handler's.

But that address isn't the C function: it's **`_sigtramp`** in libc
([`sigtramp.asm`](code/user/libc/sigtramp.asm)), which must restore
**everything**. The program was stopped at an arbitrary instruction, and
can't be allowed to notice:

```nasm
_sigtramp:                      ; stack: sig, eflags, eip
    push eax                    ; the registers a C function may change
    push ecx                    ; (it preserves ebx esi edi ebp itself)
    push edx
    sub esp, 108                ; and the x87 registers: the program may have
    fnsave [esp]                ; been interrupted in the middle of a computation
    cld                         ; C code expects the direction flag clear

    mov eax, [esp + 108 + 12]   ; sig
    push eax
    call [_sigfunc + eax * 4]   ; _sigfunc[sig](sig)
    add esp, 4

    frstor [esp]
    add esp, 108
    pop edx
    pop ecx
    pop eax
    add esp, 4                  ; drop sig
    popfd                       ; flags (CF of an interrupted system call...)
    ret                         ; to the interrupted eip
```

`signal()` in libc ([`syscall.c`](code/user/libc/syscall.c)) remembers
the C function in `_sigfunc[sig]` and gives the kernel `_sigtramp`:

```c
int (*signal(int sig, int (*func)()))()
{
    int handler = (func == SIG_DFL || func == SIG_IGN) ? (int)func : (int)_sigtramp;
    int old = syscall3(SYS_signal, sig, handler, 0);
    if (old == -1)
        return BADSIG;
    int (*prev)() = old == (int)_sigtramp ? _sigfunc[sig] : (int (*)())old;
    _sigfunc[sig] = func;
    return prev;
}
```

This is exactly V7's design. On the PDP-11, the hardware's `rtt` ("return
from trap") popped PC and PS from the user stack, so the kernel pushed
them and V7's `signal.s` ended with `rtt`. Our `popfd; ret` is the same
thing in two instructions. V7's kernel didn't pass the signal number, so
`signal.s` had one entry stub per signal to tell them apart (`tvect`);
ours does, so one entry is enough.

> **No `sigreturn`.** Modern kernels push the *whole* register state and
> the handler returns through a `sigreturn` system call that restores it.
> We get away without, because `popfd` and `ret` need no privilege. The
> one thing user mode can't restore is IF, which `popfd` silently keeps,
> and that's fine since it's always on in user mode.

The proof is [`sigtest`](code/user/bin/sigtest.c): a 20-million-term
floating-point sum, interrupted every second by a handler that itself
uses the FPU, gives the same digits as `fpu 0` (lesson 24), which nothing
interrupts. Remove the `fnsave`/`frstor` and that's no longer guaranteed.

## 5. Who sends signals

| Source | Signal | Where |
|---|---|---|
| ^C, ^\\ on the terminal | SIGINT, SIGQUIT | `tty_input` → `signal_terminal` |
| a fault in user mode | SIGSEGV, SIGFPE, SIGILL | `trap()` |
| write to a pipe without readers | SIGPIPE | `sys_write` |
| the clock, after `alarm(n)` | SIGALRM | `alarm_tick`, once a second |
| `kill(pid, sig)` | any | `sys_kill` |

**Faults** used to kill directly (lesson 13). Now they're signals like any
other, so a program can catch them. V7's Bourne shell did exactly that:
it allocated memory by touching addresses past its break, caught the
SIGSEGV, called `sbrk`, and returned to retry the instruction. The
details are printed only when the signal will kill. A fault whose signal
is *ignored* is treated as default: returning would just fault again,
forever.

**The terminal** signals "every process using it". V7 has **process
groups**: a login and all its descendants share `p_pgrp`, and the
terminal remembers its group. With one terminal and no logins, that group
is simply every process but init, which is what `signal_terminal` does.
`kill(0, sig)` means "my group": the same set.

## 6. The system calls

| Call | Does | Returns |
|---|---|---|
| `signal(sig, h)` | set the disposition (not for SIGKILL) | the previous one |
| `kill(pid, sig)` | `psignal` to pid; 0 = my group; -1 = all (root) | 0, or ESRCH/EPERM |
| `alarm(n)` | SIGALRM in n seconds; 0 cancels | seconds left on the old one |
| `pause()` | sleep until a signal | always fails with EINTR |

Only root may signal other users' processes; for anyone else, V7 just
doesn't find them (`ESRCH`, "No such process"). `kill(pid, 0)` sends
nothing but tells you whether `pid` exists.

**`fork`** copies dispositions. **`exec`** resets *caught* signals to
`SIG_DFL` (the handler's address meant something only in the old
program) but keeps *ignored* ones ignored. That one rule is how
`nohup` works, and how the shell protects background jobs:

### `sleep`, done right

V7's [`sleep.c`](code/user/libc/v7/sleep.c) replaces our busy loop:

```c
alsig = signal(SIGALRM, sleepx);    /* sleepx: longjmp(jmp, 1) */
alarm(n);
for (;;)
    pause();                        /* asleep: zero CPU */
```

(Plus care for an alarm the caller had already set.) `pause` sleeps, the
alarm's signal wakes it, `sleepx` `longjmp`s back into `sleep`, which
returns. ^P during `sleep 100` now shows it `sleeping`, not `running`.

## 7. The shell and ^C

^C goes to **every** process on the terminal: the command you're
running, but also the shell and your background jobs. So, as V7's sh did
([`sh.c`](code/user/bin/sh.c)):

- **The interactive shell catches SIGINT** and ignores SIGQUIT and
  SIGTERM. ^C at the prompt: the terminal throws the line away, the
  shell's `read()` fails with EINTR, the shell prints a new prompt.
  `kill 0` kills your jobs but not you.
- **A foreground child** sets them back to `SIG_DFL` before `exec`: ^C
  kills it. The shell, waiting for it, gets EINTR from `wait()` and
  simply waits again.
- **A background child** (`&`) sets SIGINT and SIGQUIT to `SIG_IGN`, and
  `exec` keeps that: ^C doesn't reach it. (That's the *only* protection
  background jobs had in V7.)

```c
if (interactive) {                  /* in the child, before exec */
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
}
if (background) {
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
}
```

> **Why not V7's own shell?** With signals, V7's Bourne shell has
> everything it needs from the kernel. But its source is 1979 C at its
> most PDP-11-specific: written in Algol-like macros (`IF ... THEN ...
> FI`), with struct member names shared between unrelated structs, and
> pointers and integers used interchangeably. Every file fails to compile
> with a modern compiler, and making it work would mean rewriting large
> parts, which would teach C archaeology, not operating systems. Our
> shell keeps its signal behavior.

## 8. A bug found on the way: `> /dev/null`

`yes > /dev/null` printed `y`s on the screen. `ls -l /dev` said `null`
was device `0, 0`, the console! The culprit was `itrunc` (lesson 19),
called by `creat` on an existing file:

```c
if (type == S_IFCHR || type == S_IFBLK)
    goto done;                  /* skip freeing blocks... */
...
done:
    memset(ip->d.block, 0, sizeof(ip->d.block));    /* ...but erase them anyway */
```

A device file keeps its device number in `block[]`, and the first
`> /dev/null` erased it. V7's `itrunc` returns at once for anything that
isn't a regular file or a directory; ours now does too (fixed in every
lesson since 19).

---

## 9. Modules

| File | Responsibility | Exposes |
|---|---|---|
| [`proc/signal.c`](code/kernel/proc/signal.c) | send, deliver, alarms; the system calls | `psignal signal_pending signal_deliver signal_terminal alarm_tick sys_signal sys_kill sys_alarm sys_pause` |
| [`proc/proc.c`](code/kernel/proc/proc.c) | interruptible `sleep` | `sleep` → `-EINTR` |
| [`arch/i386/trap.c`](code/kernel/arch/i386/trap.c) | faults → signals; deliver before returning | |
| [`user/libc/sigtramp.asm`](code/user/libc/sigtramp.asm) | call a handler, restore everything | `_sigtramp` |
| [`user/libc/syscall.c`](code/user/libc/syscall.c) | `signal kill alarm pause` | |
| [`user/libc/v7/sleep.c`](code/user/libc/v7/sleep.c) | `sleep` with alarm + pause | |
| [`user/bin/sigtest.c`](code/user/bin/sigtest.c) | handler transparency, EINTR | |

## 10. Test

- The milestone. ^C during `cat`, `cat | cat`, `yes > /dev/null`,
  `ed`; ^\\ during `sleep 5`.
- `sleep 100 &`, then ^C, then ^P: `sleep` is still there. `kill` its pid.
- `sigtest`; `sigtest` again, typing a line in time.
- `yes | dd count=1`: `yes` dies of SIGPIPE.
- `crash`: still "Memory fault".
- `sleep 100 &`, `su ken`, `kill 5`: "No such process". As root: killed.
- `e2fsck -fn build/disk.img`.

## Pitfalls

- **Delivering in `psignal`**: it may run in an interrupt handler, on
  another process's stack. Only mark and wake; act on the way out.
- **Forgetting to check pending signals *before* sleeping**: a ^C that
  arrived just before the `read` would be noticed only after the next
  key.
- **Not restoring eflags**: a handler interrupting `cmp; jz` changes
  where the program jumps. Not restoring the FPU: wrong numbers, rarely.
- **Clobbering EAX**: a system call interrupted by a caught signal must
  still return `EINTR` (eax) with CF set (eflags) after the handler.
- **`exec` keeping handlers**: the next signal jumps into the middle of
  the new program.
- **The shell not resetting what it ignores**: children inherit "ignore
  ^C", and nothing can be interrupted.

## References

- V7: `usr/sys/sys/sig.c` (`psignal`, `issig`, `psig`), `usr/sys/sys/sys4.c`
  (`ssig`, `kill`), `usr/sys/sys/clock.c` (`p_clktim`: alarms),
  `usr/sys/dev/tty.c` (`signal(tp->t_pgrp, SIGINT)`),
  `usr/sys/sys/machdep.c` (`sendsig`: building the frame on the user stack),
  `usr/src/libc/sys/signal.s`, `usr/src/libc/gen/sleep.c`,
  `usr/src/cmd/sh/fault.c`.
- V7 manual: `signal(2)`, `kill(2)`, `alarm(2)`, `pause(2)`.
- W. R. Stevens, *Advanced Programming in the UNIX Environment*, ch. 10:
  "unreliable signals" (exactly these), and what POSIX changed.
- xv6 has no signals, only `kill` (which sets a flag checked on the way
  out of the kernel): the simplest possible version of §1.
