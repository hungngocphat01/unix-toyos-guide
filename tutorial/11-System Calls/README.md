# 11 — System Calls

**Goal:** a user program asks the kernel to do things (`write`, `exit`)
through a **documented ABI**, and the kernel refuses bad requests instead
of crashing.

**Milestone:**

```
entering user mode at 00400000...
Hello from user mode!
writing kernel memory gave EFAULT, good
user program called exit(0)
```

**Reference code:** [`code/`](code/). New: [`abi/syscall.h`](code/abi/syscall.h)
(**the ABI**), [`abi/errno.h`](code/abi/errno.h),
[`sys/syscall.c`](code/kernel/sys/syscall.c), [`sys/sys_file.c`](code/kernel/sys/sys_file.c),
[`sys/sys_proc.c`](code/kernel/sys/sys_proc.c). Changed: [`idt.c`](code/kernel/arch/i386/idt.c)
(DPL 3 on vector 0x80), [`trap.c`](code/kernel/arch/i386/trap.c),
`vmm_check_user()` in [`vmm.c`](code/kernel/mm/vmm.c),
[`user_demo.asm`](code/kernel/user_demo.asm). The [`Makefile`](code/Makefile)
adds `-I.` so both sides can `#include "abi/..."`.

---

## 1. The mental model: a system call is an interrupt you ask for

You've already built everything. A system call is:

1. The program puts a **number** (which call) and **arguments** in registers.
2. It executes `int 0x80`. That's an interrupt like any other: the CPU switches
   to the kernel stack (TSS, lesson 10), pushes the user's SS/ESP/EFLAGS/CS/EIP,
   and jumps to `stub128` → `trap_common` → `trap(tf)`.
3. `trap()` sees `tf->trapno == 0x80` and calls `syscall(tf)`.
4. `syscall()` reads the number from `tf->eax` and the arguments from
   `tf->ebx`, `tf->ecx`... **The registers are in the trap frame because
   `pushad` put them there.**
5. It writes the result into **`tf->eax`**.
6. `trap()` returns, `trapret` does `popad` (so EAX = the result) and
   `iret`s back to the instruction after `int 0x80`.

```
 user:   mov eax, 4          ; SYS_write
         mov ebx, 1          ; fd
         mov ecx, msg        ; buf
         mov edx, 22         ; n
         int 0x80  ───────────────────────────►  trap(tf)
                                                   syscall(tf)
                                                     sys_write(tf):
                                                       fd  = tf->ebx
                                                       buf = tf->ecx  ...
                                                       return 22
                                                   tf->eax = 22
         ; eax == 22 ◄──────────────────────────  popad; iret
```

The only thing that makes vector `0x80` different from vector `0x0D` is its
gate's **DPL = 3**. For every other vector, `int N` from ring 3 is a #GP, so
user programs can't fake a page fault or a timer tick.

## 2. Designing the ABI

These are **your** decisions; the reference made the following ones and
documented them at the top of [`abi/syscall.h`](code/abi/syscall.h). Write
them down in `AGENTS.md` too, because every later lesson depends on them.

| Question | Reference choice | Why / alternatives |
|---|---|---|
| How to enter | `int 0x80` | Simple and works on every x86. `sysenter`/`syscall` are faster but need MSR setup and don't save as much state; not worth it here. |
| Call number | EAX, **V7 numbers** (`write` = 4) | Any numbering works for *source* compatibility; V7's makes V7 sources easier to compare. |
| Arguments | EBX, ECX, EDX, ESI, EDI (≤ 5) | Linux i386 does the same. The alternative (args on the user stack, like BSD) means the kernel has to read and validate user memory just to get the arguments. |
| Result | EAX | Natural for C. |
| Errors | **carry flag set, EAX = errno** | Exactly what V7 did on the PDP-11 (`sys write; bec ok; jmp cerror`). Alternative: Linux's "return −errno", which breaks for calls whose valid results can look negative. |
| Clobbered | nothing but EAX and EFLAGS | `popad` restores everything else for free. |

### Errors inside the kernel vs at the ABI

Inside the kernel it's convenient for functions to return `-EBADF` etc.
The **dispatcher** translates to the ABI, in one place
([`syscall.c`](code/kernel/sys/syscall.c)):

```c
void syscall(struct trapframe *tf)
{
    uint32_t num = tf->eax;
    int32_t ret;

    if (num < NSYSCALL && syscalls[num])
        ret = syscalls[num](tf);
    else
        ret = -EINVAL;

    if (ret < 0) {
        tf->eax = -ret;                 /* positive errno */
        tf->eflags |= EFLAGS_CF;        /* iret restores EFLAGS: user sees CF=1 */
    } else {
        tf->eax = ret;
        tf->eflags &= ~EFLAGS_CF;
    }
}
```

Setting a bit in `tf->eflags` sets the user's flag, because `iret` pops
EFLAGS from the frame. Same trick as EAX.

### The table

One function per call, all with the same shape, in a table indexed by number:

```c
typedef int32_t (*syscall_fn)(struct trapframe *tf);

static syscall_fn syscalls[NSYSCALL] = {
    [SYS_exit]   = sys_exit,
    [SYS_write]  = sys_write,
    [SYS_getpid] = sys_getpid,
};
```

Each handler unpacks its own arguments, so the register mapping is visible
right where it's used:

```c
int32_t sys_write(struct trapframe *tf)
{
    int fd = tf->ebx;
    const char *buf = (const char *)tf->ecx;
    uint32_t n = tf->edx;

    if (fd != 1 && fd != 2)
        return -EBADF;
    if (!user_ok(buf, n, 0))
        return -EFAULT;
    for (uint32_t i = 0; i < n; i++)
        console_putc(buf[i]);
    return n;
}
```

Handlers are grouped by topic: `sys_file.c` (read, write, open...),
`sys_proc.c` (exit, fork, exec...). Adding a system call from now on =
write `sys_foo`, add one line to the table.

## 3. Never trust a pointer from user space

`buf` is a number the user chose. If the kernel just reads it:

- `buf = 0xC0100000` → the kernel happily reads **its own memory** and prints
  it: a security hole (the user can't read it directly; the kernel can).
- `buf = 0x12345678` (unmapped) → page fault **in kernel mode** → panic.
  Any user can crash the machine.

So before touching user memory, check that **every page** of
`[buf, buf+n)` is below `KERNBASE`, present, and has the **U** bit (and
**W** if the kernel will write into it, e.g. `read()`). That's
`vmm_check_user()` in [`vmm.c`](code/kernel/mm/vmm.c):

```c
int vmm_check_user(pde_t *pd, uint32_t va, uint32_t len, int write)
{
    uint32_t end = va + len;
    uint32_t need = PTE_P | PTE_U | (write ? PTE_W : 0);

    if (end < va || end > KERNBASE)         /* wraps around, or reaches the kernel */
        return -1;
    for (uint32_t page = PAGE_ROUND_DOWN(va); page < end; page += PAGE_SIZE) {
        pte_t *pte = walk(pd, page, 0);
        if (!pte || (*pte & need) != need)
            return -1;
    }
    return 0;
}
```

After the check, the kernel reads `buf` **directly**: we're still in the
user's address space (same CR3), and the kernel half is mapped too.

(Linux instead lets the access fault and recovers through an exception
table. The up-front check is simpler and good enough for a single-CPU toy:
nothing can unmap the page between the check and the use. Once you have
multiple threads in one address space, that changes.)

---

## 4. Modules

| File | Responsibility | Exposes |
|---|---|---|
| [`abi/syscall.h`](code/abi/syscall.h) | the ABI comment + all V7 call numbers | `SYS_*`, `SYSCALL_VECTOR` |
| [`abi/errno.h`](code/abi/errno.h) | V7 error numbers | `E*` |
| [`sys/syscall.c`](code/kernel/sys/syscall.c) | table, dispatcher, user-pointer check | `syscall(tf)`, `user_ok(p, len, write)` |
| [`sys/sys_file.c`](code/kernel/sys/sys_file.c) | `write` (console only for now) | `sys_write` |
| [`sys/sys_proc.c`](code/kernel/sys/sys_proc.c) | `exit` (halts for now), `getpid` | `sys_exit sys_getpid` |
| [`arch/i386/idt.c`](code/kernel/arch/i386/idt.c) | gate 0x80 with DPL 3 | |
| [`arch/i386/trap.c`](code/kernel/arch/i386/trap.c) | `case T_SYSCALL: syscall(tf)` | |
| [`mm/vmm.c`](code/kernel/mm/vmm.c) | `vmm_check_user()` | |

`abi/` is the only directory shared by kernel and user space. The kernel
includes it as `"abi/syscall.h"` (thanks to `-I.`); user programs will too
(lesson 12).

## 5. Test

- The milestone. The user program checks the carry flag and EAX itself
  ([`user_demo.asm`](code/kernel/user_demo.asm)).
- Call an unused number (e.g. 63): CF set, `EAX = EINVAL`.
- Remove `user_ok()` from `sys_write`: the second write prints kernel
  bytes. Then try `ecx = 0x12345678`: kernel page fault, panic. Put it back.
- Change the gate's DPL to 0: the first `int 0x80` is a #GP from user mode
  (`err=402`: the error code is the IDT index × 8 + 2).

## Pitfalls

- **Gate DPL left at 0**: #GP with error code `0x402` on the first call.
- **Forgetting to clear CF on success**: if the user's CF happened to be set
  before `int 0x80`, a successful call looks like a failure.
- **Trusting `len`**: `buf = 0x3FFFF000, len = 0xFFFFFFFF` wraps around.
  Check `end < va`.
- **Checking only the first page** of a buffer that spans two.
- Interrupts are **off** during a system call (interrupt gate). Fine while
  syscalls are quick; lesson 18 (`read` waiting for the keyboard) has to deal
  with it.

## References

- V7: `usr/sys/sys/trap.c` (`trap()` → `sysent[]` dispatch, the `u.u_error` →
  carry flag logic), `usr/sys/sys/sysent.c`, `usr/src/libc/sys/write.s`.
- xv6: `syscall.c` (`argint`, `argptr`, `fetchstr`), `syscall.h`, `usys.S`.
- Linux i386 ABI: `man 2 syscall`, section "Architecture calling conventions".
