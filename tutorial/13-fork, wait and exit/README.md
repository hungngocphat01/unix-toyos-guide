# 13 — fork, wait and exit

**Goal:** the Unix process trio. A process can create a copy of itself,
the copy can run another program, and the parent learns how it ended. That's
everything a shell needs to run commands.

**Milestone:**

```
init: I am pid 1
child: fork returned 0, my pid is 2, counter = 100
init: fork returned 2; my counter is still 1; child exit code 5
hello: pid 3, argv[0]="hello" argv[1]="world"
init: pid 3 (hello) exited with code 3
trap 14 (page fault), err=4
  address=00000000 (page not present, read, user mode)
  ...
pid 4 (crash) killed
init: pid 4 (crash) was killed by signal 11
init: cannot exec missing, errno 2
init: pid 5 (missing) exited with code 127
init: wait with no children: errno 10 (ECHILD is 10)
init exited with status 0; halting
```

**Reference code:** [`code/`](code/). Changed: [`proc/proc.c`](code/kernel/proc/proc.c)
(`proc_fork`, `proc_wait`, `proc_exit` resumes the parent),
[`proc/proc.h`](code/kernel/proc/proc.h) (`SUSPENDED`, `saved_tf`),
`uvm_clone` in [`mm/uvm.c`](code/kernel/mm/uvm.c),
`sys_fork`/`sys_wait` in [`sys/sys_proc.c`](code/kernel/sys/sys_proc.c),
user [`init.c`](code/user/bin/init.c) (test driver), [`crash.c`](code/user/bin/crash.c),
`fork`/`wait` in [`user/libc/syscall.c`](code/user/libc/syscall.c).

---

## 1. The three calls

```c
int pid = fork();          /* returns TWICE: child's pid in the parent, 0 in the child */
if (pid == 0) {
    exec("hello", argv);   /* child: become another program */
    _exit(127);            /* only reached if exec failed */
}
int status;
wait(&status);             /* parent: sleep until a child exits, collect its status */
```

### "fork returns twice" is just two trap frames

`fork()` is a system call, so when it starts, the parent's registers are in
the trap frame. The kernel:

1. creates a new `struct proc` and a **copy of the address space**
   (`uvm_clone`: a new frame for every user page, contents copied);
2. ends up with **two** saved register sets that are identical except EAX:

```
 parent's trap frame:  eip = after "int 0x80", esp = ..., eax = 2 (child's pid)
 child's trap frame:   eip = after "int 0x80", esp = ..., eax = 0
```

When each of them is resumed (`iret`), it continues right after `int 0x80`
with its own EAX. Both believe they just returned from `fork()`. Same
`eip`, same `esp`, same stack *contents*, but in different frames.
That's why `counter = 100` in the child doesn't change the parent's `counter`.

## 2. Sequential fork: no scheduler needed (yet)

A real Unix runs parent and child **at the same time**. That needs a
scheduler, a kernel stack per process and a way to block in the kernel
(lessons 23–24). We can postpone all of that with one rule:

> **After `fork()`, the child runs until it exits. The parent is suspended
> until then.**

It's what `vfork` + "the shell always waits" looks like, and it's enough for
a shell running one command at a time.

```
 time ──►
 pid 1  init:  fork() ··· SUSPENDED (saved_tf.eax = 2) ··················· resumes: fork() = 2, then wait() = 2
 pid 2                  child: fork() = 0 → exec("hello") → ... → exit(3) → ZOMBIE
```

The implementation is "swap which trap frame is on the kernel stack":

**fork** ([`proc.c`](code/kernel/proc/proc.c)):

```c
/* Suspend the parent: when it resumes, fork() returns the child's pid. */
struct trapframe *tf = proc_trapframe();
parent->saved_tf = *tf;
parent->saved_tf.eax = child->pid;
parent->saved_tf.eflags &= ~EFLAGS_CF;
parent->state = SUSPENDED;

/* Switch to the child. */
child->state = RUNNING;
current = child;
write_cr3(V2P(child->pd));
return 0;                   /* syscall() puts this in tf->eax: the CHILD's return value */
```

The trap frame on the kernel stack doesn't move: it now simply *belongs to
the child* (it's an exact copy of the parent's anyway), and returning from
the system call returns into the child's address space.

**exit** puts the parent's saved registers back and leaves through the usual
exit path:

```c
parent->state = RUNNING;
current = parent;
write_cr3(V2P(parent->pd));
*proc_trapframe() = parent->saved_tf;
enter_usermode(proc_trapframe());        /* = jmp trapret: iret into the parent */
```

`enter_usermode` resets ESP, so it doesn't matter how deep in the kernel
`proc_exit` was called from (a syscall, or `trap()` killing a faulting
process).

Invariant: **the running process is always the youngest; every other live
process is suspended in `fork()`**. So when the parent runs again, its
child is already a zombie, and `wait` never has to block. `proc_wait`
panics if that's ever false: an invariant check, not an error path.

## 3. Zombies, wait status and orphans

**Why zombies?** When a process exits, its memory is freed right away, but
its **slot** stays (state `ZOMBIE`) holding the pid and exit status until the
parent calls `wait`. Otherwise the parent couldn't learn how its child ended.

**The status word** (V7 format, set by `proc_exit`):

```
 bits 15..8: exit code  (exit(3) → 0x0300)
 bits  7..0: signal     (killed by SIGSEGV → 0x000b), 0 if it exited normally
```

**Orphans:** if a process exits while it still has children (zombies, in our
model), they're given to **init** (pid 1), which will `wait` for them. A real
init loops on `wait()` forever for this reason (lesson 21).

**ABI note:** V7's `wait` returned the status in a second register (`r1`);
the C library stored it through the pointer. We pass the pointer to the
kernel directly: `wait(int *status)`, which may be NULL.

## 4. Copying an address space

[`uvm_clone()`](code/kernel/mm/uvm.c) walks the parent's page directory
below `KERNBASE`, and for every present page: allocates a frame, copies the
4 KiB through the direct map, maps it at the same address with the same
flags. The kernel half comes from `vmm_new_pd()` as usual.

It's wasteful when the child immediately calls `exec` (copy everything,
then throw it away). Real systems use **copy-on-write**: share the frames
read-only and copy a page only when someone writes to it. That's a nice
extension once page faults can be resolved instead of fatal (the `avail`
bits in the PTE can mark "COW").

---

## 5. Modules

| File | Change | Exposes |
|---|---|---|
| [`proc/proc.h`](code/kernel/proc/proc.h) | `SUSPENDED` state; `saved_tf` | `proc_fork()`, `proc_wait(&status)` |
| [`proc/proc.c`](code/kernel/proc/proc.c) | fork/wait; exit resumes the parent, reparents children to init | |
| [`mm/uvm.c`](code/kernel/mm/uvm.c) | copy an address space | `uvm_clone(pd)` |
| [`sys/sys_proc.c`](code/kernel/sys/sys_proc.c) | `sys_fork`, `sys_wait` | |
| [`user/libc/syscall.c`](code/user/libc/syscall.c) | `fork()`, `wait(&status)` | |
| [`user/bin/init.c`](code/user/bin/init.c) | test driver: fork-only, fork+exec+wait, crash, missing program, ECHILD | |

## 6. Test

- The milestone.
- Nested: in the forked child, fork again and wait. Three levels deep
  should work.
- Fork bomb: a loop of `fork()` where each child immediately exits: pids
  keep increasing, slots are reused, `pmm_free_count()` returns to the same
  number (print it in `proc_exit` to check for leaks).
- Fork bomb that doesn't exit (each child forks again): eventually
  `EAGAIN` (table full, 64 processes) or `ENOMEM`. The kernel must survive.

## Pitfalls

- **Returning the pid in the child and 0 in the parent** (swapped).
  Write down which trap frame goes where.
- **Forgetting to clear CF** in the parent's saved frame: if the parent's
  CF happened to be set, `fork()` "fails" with errno = child pid.
- **Freeing the page directory you're running on** in exit (CR3 first).
- **Reusing a zombie's slot before the parent waited**: the parent's `wait`
  then can't find it, or finds a different process.
- **Leaking**: `uvm_clone` failing halfway must free what it allocated.

## References

- V7: `usr/sys/sys/sys1.c` (`fork`, `wait`, `exit`), `usr/sys/sys/slp.c`
  (`newproc`: V7 copied the whole process image, or swapped it out).
- xv6: `proc.c` (`fork`, `exit`, `wait`), `vm.c` (`copyuvm`).
- The original paper: Ritchie & Thompson, "The UNIX Time-Sharing System"
  (CACM 1974), section on processes.
