# 23 — Kernel Stacks and the Scheduler

**Goal:** several processes alive **and runnable** at once. Each process
gets its **own kernel stack**; a **context switch** (`swtch`, 13
instructions) moves the CPU from one kernel stack to another; a
**scheduler** loop picks who runs next. `fork` no longer suspends the
parent, and the shell gets `&` and `wait`.

**Milestone:**

```
$ ls -l /bin > /tmp/l & echo started
5
started
$ wait
$ wc /tmp/l
     48    378    2103 /tmp/l
$ sh -c "cat /etc/motd; echo bye" &
8
$ Welcome to toyos!               <- the job runs while the shell waits for you
bye
```

and ^P (Ctrl-P) prints the process table:

```
  PID  PPID  STATE     NAME
    1     0  runnable  init
    4     1  runnable  sh
```

**Reference code:** [`code/`](code/). New:
[`proc/swtch.asm`](code/kernel/proc/swtch.asm). Rewritten:
[`proc/proc.c`](code/kernel/proc/proc.c). Changed:
[`proc/proc.h`](code/kernel/proc/proc.h) (states, `kstack`, `tf`,
`context`), [`proc/exec.c`](code/kernel/proc/exec.c) (`p->tf`),
[`dev/tty.c`](code/kernel/dev/tty.c) (waits with `yield`; ^P),
[`kmain.c`](code/kernel/kmain.c) (ends in `scheduler()`),
[`user/bin/sh.c`](code/user/bin/sh.c) (`&`, `wait`).
Removed: `enter_usermode` (isr.asm).

---

## 1. Why one kernel stack per process

Since lesson 10, a process entering the kernel gets its trap frame at the
top of **the** kernel stack, and the kernel's C code runs below it. That
works while only one process is ever *in the kernel*. Sequential `fork`
(lesson 13) kept it that way, at a price: the parent can't run until the
child exits.

Now suppose the shell is in the kernel, inside `read()`, waiting for a
key, and we'd like a background job to run meanwhile. The shell's
**kernel** state (the `read` call in progress: `sys_read` → `file_read`
→ `tty_read` → the waiting loop, with their local variables and return
addresses) lives on the kernel stack. If the background job entered the
kernel on the same stack, it would overwrite all that.

So **each process gets its own kernel stack** (one page, `p->kstack`).
A process that stops in the middle of a system call keeps its whole
kernel call chain on its own stack, frozen, and resumes it later.

```
  shell's kernel stack           background job's kernel stack
 ┌─────────────────────┐        ┌─────────────────────┐
 │ trap frame (user    │        │ trap frame          │
 │ registers of read())│        │                     │
 │ syscall()           │        │ syscall()           │
 │ sys_read()          │        │ sys_write()         │
 │ file_read()         │        │ ...                 │
 │ tty_read()          │        │                     │
 │ yield() / swtch()   │        │                     │
 │ saved context  ◄────┼─ p->context                  │
 └─────────────────────┘        └─────────────────────┘
```

The TSS's `esp0` (lesson 10) must point at the **running** process's
stack, so that its next trap lands there. The scheduler sets it before
each switch.

## 2. The context switch: `swtch`

```c
void swtch(struct context **old, struct context *new);
```

From the caller's point of view, `swtch` is a function that returns
**later, when someone switches back**. It's the strangest function in
the kernel, and only 13 instructions ([`swtch.asm`](code/kernel/proc/swtch.asm)):

```nasm
swtch:
    mov eax, [esp + 4]          ; old
    mov edx, [esp + 8]          ; new
    push ebp                    ; save the callee-saved registers on OUR stack,
    push ebx                    ; below the return address that "call swtch"
    push esi                    ; already pushed: that's a struct context
    push edi
    mov [eax], esp              ; *old = where our context is
    mov esp, edx                ; ---- now on the other stack ----
    pop edi                     ; its context, saved by ITS earlier swtch()
    pop esi
    pop ebx
    pop ebp
    ret                         ; return into whoever called swtch() there
```

Why save only `ebp ebx esi edi`? The C calling convention says a function
may clobber `eax ecx edx`; the caller saved them if it needed them. So to
"pause" a C function in the middle of a call to `swtch`, it's enough to
keep the callee-saved registers, the stack pointer (stored in `*old`) and
the return address (already on the stack). The **user** registers aren't
part of it: they're in the trap frame at the top of the same stack,
untouched.

Follow one switch, A → scheduler → B:

```
1. A calls yield() → sched() → swtch(&A->context, scheduler_context)
   A's context is pushed on A's stack; A->context points at it.
   esp = scheduler_context; pops; ret → we're in scheduler(), right after
   ITS previous swtch() call.
2. scheduler() picks B, sets cr3 and esp0 for B, calls
   swtch(&scheduler_context, B->context)
   The scheduler's context is saved; esp = B->context; pops; ret →
   we're in B, right after the swtch() B called when it last stopped
   (in its own sched()). B returns from sched(), from yield()... and
   eventually from its system call, to user mode.
```

The scheduler has its own context, on the boot stack: `kmain` ends by
calling `scheduler()`, which never returns.

## 3. A new process's first run

A process that has never run has never called `swtch`, so there's no
saved context to return into. We **fake one** ([`proc_alloc`](code/kernel/proc/proc.c)):

```
 kstack + 4096  ┌──────────────────┐
                │ struct trapframe │ <- p->tf: user registers (fork copies the
                │                  │    parent's, exec builds a fresh one)
                ├──────────────────┤
                │ edi esi ebx ebp  │ <- p->context
                │ eip = trapret    │
                └──────────────────┘
```

The first `swtch` to it pops four zeros and `ret`s to **`trapret`**,
the second half of `trap_common` (lesson 4): it pops the trap frame and
`iret`s to user mode. The new process "returns from an interrupt" it
never took, exactly what `enter_usermode` did, now through the normal
path. (xv6 puts a small C function, `forkret`, in between, to release a
lock; we have no locks.)

### `fork` returns twice, explained

```c
*child->tf = *parent->tf;       /* same user registers... */
child->tf->eax = 0;             /* ...except fork() returns 0 */
child->tf->eflags &= ~EFLAGS_CF;
child->state = RUNNABLE;
return child->pid;              /* the parent's return value */
```

The parent returns from the system call normally. The child, when the
scheduler first picks it, enters user mode through `trapret` with a copy
of the parent's registers: same `eip` (just after `int 0x80` in `fork`),
same `esp`, and `eax = 0`. Two processes, each returning from one `fork`.

## 4. The scheduler

```c
void scheduler(void)
{
    for (;;) {
        __asm__ volatile ("sti; nop; cli");     /* let pending interrupts in */
        p = the next RUNNABLE process after the last one (round robin);
        if (!p) {
            __asm__ volatile ("sti; hlt");      /* idle until an interrupt */
            continue;
        }
        current = p;
        p->state = RUNNING;
        write_cr3(V2P(p->pd));                  /* its address space */
        tss_set_kernel_stack(top of p->kstack); /* its kernel stack for traps */
        swtch(&scheduler_context, p->context);
        current = NULL;                         /* p gave up the CPU */
        write_cr3(V2P(kernel_pd));
    }
}
```

A process gives the CPU back with `sched()` after setting its state:
`yield()` (state `RUNNABLE`: "run me again later"), or `exit` (`ZOMBIE`:
"never again").

**Interrupts** are off whenever we touch the process table, since an
interrupt handler could change it. System calls already run with
interrupts off (interrupt gate), and so does the scheduler, except for
one deliberate window each round: `sti; nop; cli` (`sti` takes effect
after the *next* instruction, hence the `nop`). Without it, if every
process were busy yielding, interrupts would never be handled again: no
keyboard, no clock.

## 5. wait and exit

The states are now `UNUSED → RUNNABLE ⇄ RUNNING → ZOMBIE → UNUSED`.

**exit** closes files, frees the address space, re-parents its children
to init, becomes `ZOMBIE` and calls `sched()` for the last time. It can't
free its **kernel stack**: it's still running on it! The parent frees it
in `wait`, together with the slot.

**wait** looks for a `ZOMBIE` child; if it has living children but none
has exited, it `yield()`s and looks again.

## 6. Cooperative, and polling: what's still wrong

Two weaknesses, both on purpose, both fixed in lesson 24:

1. **Waiting is polling.** `wait` and `tty_read` loop on `yield()`. In
   the ^P output above, init and sh are both `runnable`: the scheduler
   keeps switching to them just so they can check and yield again.
2. **Nobody is forced to give up the CPU.** A process only switches when
   it waits. A program computing in user mode keeps the CPU until it
   exits. Try it:

   ```
   $ sleep 3 &          <- our stand-in sleep spins on time()
   5
   $ date               <- typed immediately: echoed, but runs 3 s later
   ```

   (The echo appears at once because echoing happens in the keyboard
   interrupt handler, not in the shell.)

The fix for 1 is **sleep/wakeup**: a waiting process sleeps on a
"channel" and isn't runnable until someone wakes it. The fix for 2 is
**preemption**: the timer interrupt makes the running process `yield()`.

> **And the FPU?** User programs may use the x87 registers. A switch
> only happens inside a system call, i.e. inside a C function call, where
> the calling convention leaves the x87 stack empty, so nothing is lost
> yet. With preemption, a switch can happen in the middle of a
> computation, and lesson 24 must save the FPU state.

---

## 7. Modules

| File | Responsibility | Exposes |
|---|---|---|
| [`proc/swtch.asm`](code/kernel/proc/swtch.asm) | save one kernel context, load another | `swtch` |
| [`proc/proc.c`](code/kernel/proc/proc.c) | process table, kernel stacks, scheduler, fork/wait/exit | `scheduler yield proc_fork proc_wait proc_exit proc_start_init proc_dump` |
| [`dev/tty.c`](code/kernel/dev/tty.c) | waits by yielding; ^P dumps processes | |
| [`user/bin/sh.c`](code/user/bin/sh.c) | `cmd &`, `wait` | |

## 8. Test

- The milestone; ^P at various moments.
- `ls > /tmp/a & ls > /tmp/b & wait`: both files complete.
- Start 70 background jobs (`NPROC` is 64): `fork` fails with `EAGAIN`,
  and the shell says "cannot fork".
- Run `e2fsck` after a session with background jobs writing files.
- In GDB, `break swtch`, then `x/5x $esp` after the `push`es: you're
  looking at a `struct context`. `stepi` past `mov esp, edx` and `bt`:
  the backtrace is the *other* process's.

## Pitfalls

- **Forgetting `tss_set_kernel_stack`** on a switch: the next trap from
  user mode lands on another process's kernel stack and silently corrupts it.
- **Freeing the kernel stack in `exit`**: you're standing on it.
- **`swtch` with interrupts on**: an interrupt arriving between `mov esp`
  and the `pop`s runs on a half-switched stack. Keep them off (`sched`
  does `cli`).
- **A process that never yields and interrupts never enabled in the
  scheduler**: the machine freezes; hence the `sti; nop; cli` window.
- **Wrong fake context layout**: `struct context` order must match the
  `push`/`pop` order in `swtch` exactly.
- **`current` in interrupt handlers**: it's `NULL` while the scheduler
  runs. Check `from_user(tf)` before using it.

## References

- xv6: `proc.c` (`scheduler`, `sched`, `yield`, `allocproc`, `fork`),
  `swtch.S`. The xv6 book, chapter 7 "Scheduling", the clearest
  explanation there is.
- V7: `usr/sys/sys/slp.c` (`swtch`, `sleep`, `wakeup`), `usr/sys/conf/mch.s`
  (`save`, `resume`: the PDP-11 context switch).
- Lions' Commentary, chapters 7–8 ("Process Management"), including the
  famous comment "You are not expected to understand this" in V6's `swtch`.
