# 24 — Sleep, Wakeup and Preemption

**Goal:** fix the two weaknesses lesson 23 left on purpose. A process that
waits **sleeps** and costs nothing until an event **wakes** it. The
**clock preempts** a process that computes too long. And because a
process can now be stopped anywhere, the kernel saves its **FPU
registers** too.

**Milestone:**

```
$ sleep 3 &                  <- computes for 3 s (our stand-in spins)
5
$ date                       <- runs at once now, not 3 s later
Wed Nov 11 15:06:34 GMT 2026
$ ^P
  PID  PPID  STATE     NAME
    1     0  sleeping  init  <- in wait(): no longer polling
    4     1  sleeping  sh    <- in read() on the terminal
    5     4  running   sleep
$ fpu 1; fpu 2               <- one after the other
fpu 1: 16.3884585714
fpu 2: 15.8884586214
$ fpu 1 & fpu 2 & wait       <- at the same time, preempting each other
7
8
fpu 1: 16.3884585714         <- same results
fpu 2: 15.8884586214
```

**Reference code:** [`code/`](code/). New:
[`arch/i386/fpu.c`](code/kernel/arch/i386/fpu.c) and
[`fpu.h`](code/kernel/arch/i386/fpu.h),
[`user/bin/fpu.c`](code/user/bin/fpu.c). Changed:
[`proc/proc.c`](code/kernel/proc/proc.c) and
[`proc.h`](code/kernel/proc/proc.h) (`SLEEPING`, `sleep`, `wakeup`, FPU
state), [`arch/i386/trap.c`](code/kernel/arch/i386/trap.c) (preemption),
[`dev/tty.c`](code/kernel/dev/tty.c) (sleeps instead of polling),
[`proc/exec.c`](code/kernel/proc/exec.c) (fresh FPU),
[`arch/i386/cpu.h`](code/kernel/arch/i386/cpu.h) (CR0/CR4 bits),
[`kmain.c`](code/kernel/kmain.c) (`fpu_init`), the
[`Makefile`](code/Makefile) (`-mgeneral-regs-only`).

---

## 1. sleep and wakeup

V7's whole synchronization story is two functions
([`proc.c`](code/kernel/proc/proc.c)):

```c
void sleep(void *chan)
{
    current->chan = chan;
    current->state = SLEEPING;      /* the scheduler skips us...        */
    sched();
    current->chan = NULL;           /* ...until wakeup() made us RUNNABLE */
}

void wakeup(void *chan)
{
    for (each process p)
        if (p->state == SLEEPING && p->chan == chan)
            p->state = RUNNABLE;
}
```

A **wait channel** is just an address that both sides agree on. It's never
dereferenced; it only names the event:

| Waiting for | Sleeps on | Woken by |
|---|---|---|
| a line on the terminal | `&tty` | `tty_input` (keyboard/serial interrupt), when a line completes |
| a child to exit | its own `struct proc` | `proc_exit` of the child: `wakeup(p->parent)` |
| orphans to collect (init) | init's `struct proc` | `proc_exit`, when it hands children to init |

`wakeup` wakes **everybody** on the channel, and doesn't promise the
event is still there when they run (another process may have consumed
the line already). So **sleep is always in a loop that re-checks**:

```c
static void wait_for_input(void)
{
    while (tty.r == tty.w)          /* nothing to read */
        sleep(&tty);
}
```

### The lost wakeup

What if the key arrives *between* the test and the sleep?

```
tty_read:  tty.r == tty.w? yes ...
                      ───► keyboard interrupt: tty_input; wakeup(&tty):
                           nobody is sleeping yet, nothing happens
tty_read:  ... sleep(&tty)      zzz forever (until the NEXT key)
```

This is the classic bug of sleep/wakeup. Our protection: **interrupts are
off** in the kernel (system calls enter through an interrupt gate), so
no interrupt can run between the test and the `sleep`. They come back
on only in the scheduler, after we're safely `SLEEPING`. V7 did the same
with `spl5()` around the test (raising the priority level so the
terminal's interrupt waits). On a multiprocessor this isn't enough, and
xv6's `sleep` takes a lock as its second argument, for the same reason.

## 2. Preemption

The timer interrupts 100 times a second. If it interrupted a process in
**user mode**, that process has had its turn
([`trap.c`](code/kernel/arch/i386/trap.c)):

```c
if (irq_handlers[irq])
    irq_handlers[irq]();
if (irq == IRQ_TIMER && from_user(tf))
    yield();                /* back of the queue; returns on our next turn */
return;
```

`yield()` runs on the process's own kernel stack, inside the interrupt,
with the user registers safely in the trap frame. When the scheduler
picks it again, `yield` returns, `trap` returns, and `trapret` `iret`s
back to where the program was interrupted, which never notices.

Two details:

- **EOI before the switch.** `trap()` sends the end-of-interrupt to the
  PIC *before* calling the handler. Otherwise the PIC would block further
  timer interrupts until this process ran again.
- **Only from user mode.** The kernel is **not preemptible**: a process in
  kernel code runs until it sleeps or returns to user mode. All our kernel
  code (the buffer cache, inode table, `balloc`...) assumes nobody else
  touches the shared tables in the middle. That's how V7 worked too, and
  it's why one CPU and "interrupts off" was enough for V7: no locks.

V7 was less eager: a process ran until it slept or its priority dropped
(computed from recent CPU use, once per second, in `clock()`). We switch
at every tick, round robin. Simpler; the price is a context switch every
10 ms when two processes compute.

## 3. The FPU belongs to the process

Until now, a switch only happened inside a system call, i.e. in the
middle of a C function call, where the x87 register stack is empty by
convention. With preemption, a process can be stopped at any instruction,
in the middle of `sum += 1.0 / (i + k)`, with its running sum in `st(0)`.
If the next process uses the FPU, that sum is gone.

So the FPU's registers are part of the process's state, like `eax`. The
CPU saves and restores all of them (x87, MMX and SSE, 512 bytes) with
two instructions ([`fpu.c`](code/kernel/arch/i386/fpu.c)):

```c
void fpu_save(struct fpu_state *s)          { asm volatile ("fxsave %0"  : "=m"(*s)); }
void fpu_restore(const struct fpu_state *s) { asm volatile ("fxrstor %0" : : "m"(*s)); }
```

used around every switch in the scheduler:

```c
fpu_restore(&p->fpu);
swtch(&scheduler_context, p->context);
fpu_save(&p->fpu);
```

and at the start of a process's life: a new process starts with
`fpu_initial` (the state right after `fninit`), `fork` copies the
parent's live registers (`fpu_save(&child->fpu)`), and `exec` loads
`fpu_initial` (a new program shouldn't inherit the old one's FPU state).

Setup, once ([`fpu_init`](code/kernel/arch/i386/fpu.c)):

| Bit | Meaning |
|---|---|
| `CR0.EM = 0` | an FPU exists; don't trap on FPU instructions |
| `CR0.MP = 1` | (with `TS`, for lazy switching; see below) |
| `CR0.NE = 1` | report FPU errors as exception 16, not through the PIC |
| `CR4.OSFXSR = 1` | the OS knows `fxsave`/`fxrstor` (required to use them, and SSE) |

`fxsave`'s buffer must be **16-byte aligned**: `struct fpu_state` says
`__attribute__((aligned(16)))`, and that alignment propagates to
`struct proc` and the process table.

**The kernel must not use the FPU itself**: nothing would save the user's
registers before the kernel clobbered them. GCC can use SSE registers
even for integer code (copying structs, for instance), so the kernel is
compiled with **`-mgeneral-regs-only`**, which makes any FPU use a
compile error.

> **Lazy switching.** Most processes never touch the FPU, and 512 bytes
> per switch adds up. Real kernels used to set `CR0.TS` on a switch: the
> next FPU instruction traps (exception 7, "device not available"), and
> only then is the FPU switched. Modern kernels went back to eager
> switching, partly because of side channels (the "LazyFP" attack, 2018).

### Proving it

`fpu` sums 20 million terms, keeping the sum in a register. Run two at
once, preempting each other ~100 times per second: same results as one at
a time. Now comment out the `fpu_restore`/`fpu_save` lines in
`scheduler()` and run them together again:

```
fpu 1: 23.1374988119
fpu 2: 0.0090885751
```

Each process continued with the other's registers.

## 4. What still polls

- **The disk.** `ata_read` busy-waits on the status register. A
  sleeping disk driver (start the transfer, `sleep(buf)`, the ATA
  interrupt `wakeup`s) would let others run during I/O, but then two
  processes could be inside the filesystem at once: one asleep halfway
  through allocating a block, the other changing the same bitmap. V7
  handles that with a busy flag on every buffer (`B_BUSY`) and a lock on
  every inode (`ILOCK`), each with sleep/wakeup. A good exercise once
  everything else works; our disk is a RAM-backed QEMU image, fast enough.
- **Our `sleep` command.** It spins on `time()`. With preemption that's
  merely wasteful, not blocking. Lesson 26 replaces it with V7's
  `alarm` + `pause`, which really sleep.

---

## 5. Modules

| File | Responsibility | Exposes |
|---|---|---|
| [`proc/proc.c`](code/kernel/proc/proc.c) | sleep/wakeup; FPU around switches | `sleep wakeup` |
| [`arch/i386/fpu.c`](code/kernel/arch/i386/fpu.c) | enable the FPU; save/restore | `fpu_init fpu_save fpu_restore fpu_initial` |
| [`arch/i386/trap.c`](code/kernel/arch/i386/trap.c) | preempt on the timer | |
| [`dev/tty.c`](code/kernel/dev/tty.c) | `sleep(&tty)` / `wakeup(&tty)` | |
| [`user/bin/fpu.c`](code/user/bin/fpu.c) | FPU switching test | |

## 6. Test

- The milestone. ^P while idle: everything `sleeping`.
- Idle CPU: with nothing to do, QEMU's CPU use (in `top` on the host)
  should be near zero: the scheduler `hlt`s.
- `fpu 1 & fpu 2 & fpu 3 & wait` and compare with sequential runs.
- Remove the FPU switching (as above) and see the test fail.
- Remove `wakeup(p->parent)` from `exit`: the shell hangs after the first
  command. Remove `wakeup(&tty)`: the first key never arrives.
- Background jobs writing files, then `e2fsck -fn build/disk.img`.

## Pitfalls

- **`if` instead of `while` around `sleep`**: woken, but the event was
  for someone else. Always re-check.
- **Enabling interrupts between the check and the sleep**: lost wakeup.
- **Preempting kernel code**: the kernel's tables get corrupted, rarely
  and unreproducibly. Check `from_user(tf)`.
- **EOI after `yield`**: no more timer interrupts until the process runs
  again, and with only one process... no more at all.
- **`fxsave` on an unaligned buffer**: general protection fault.
- **Forgetting `CR4.OSFXSR`**: user programs' SSE instructions become
  invalid opcodes, and `fxsave` may not save the SSE registers.
- **Kernel code using SSE** behind your back: `-mgeneral-regs-only`.

## References

- V7: `usr/sys/sys/slp.c` (`sleep`, `wakeup`, `setrun`, `swtch`),
  `usr/sys/sys/clock.c` (priority recomputation, `runrun`),
  `usr/sys/dev/tty.c` (`sleep(&tp->t_rawq, TTIPRI)`).
- xv6: `proc.c` (`sleep`, `wakeup`, `yield`), `trap.c` (preemption on
  `IRQ_TIMER`). The xv6 book, chapter 7, "Sleep and wakeup", on the lost
  wakeup.
- Intel SDM vol. 1, ch. 10.5 (`FXSAVE`); vol. 3, ch. 13 (FPU, `CR0.TS`,
  `CR4.OSFXSR`).
- Lions' Commentary, chapter 8 ("Process Management"): sleep, wakeup and
  "runrun".
