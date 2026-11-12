# 12 — Processes and exec

**Goal:** real programs, written in C and compiled separately from the
kernel, loaded from ELF files, with arguments, environment and a heap.

**Milestone:**

```
bootmod: hello (22496 bytes)
bootmod: init (17632 bytes)
...
starting init as pid 1
init: I am pid 1, argc=1, argv[0]=init
hello: pid 1
  argv[0] = "hello"
  argv[1] = "one"
  argv[2] = "two"
  envp[0] = "HOME=/"
  envp[1] = "USER=root"
hello: sbrk gave 40100c, wrote 100 KiB there
hello: exec("nope") failed with errno 2 (ENOENT is 2)
pid 1 (hello) exited, status 300
no processes left; halting
```

`init` exec'd `hello`: same process (pid 1), new program. `status 300` is
V7's encoding of `exit(3)`.

**Reference code:** [`code/`](code/). This is a big lesson; here's the map.

| Area | Files |
|---|---|
| Processes | [`proc/proc.h`](code/kernel/proc/proc.h), [`proc/proc.c`](code/kernel/proc/proc.c) |
| ELF loading | [`proc/elf.h`](code/kernel/proc/elf.h), [`proc/exec.c`](code/kernel/proc/exec.c) |
| Boot modules | [`proc/bootmod.c`](code/kernel/proc/bootmod.c) |
| User memory | [`mm/uvm.h`](code/kernel/mm/uvm.h), [`mm/uvm.c`](code/kernel/mm/uvm.c) |
| Syscalls | `exec`, `exece`, `brk` in [`sys/sys_proc.c`](code/kernel/sys/sys_proc.c); `fetch_string` in [`sys/syscall.c`](code/kernel/sys/syscall.c) |
| Faults kill processes | [`arch/i386/trap.c`](code/kernel/arch/i386/trap.c) |
| User space | [`user/user.ld`](code/user/user.ld), [`user/libc/`](code/user/libc/), [`user/bin/`](code/user/bin/) |
| Build | [`Makefile`](code/Makefile) (user programs, libc.a, `-initrd`) |
| ABI | [`abi/signal.h`](code/abi/signal.h) (new) |

---

## 1. What is a process?

Everything the kernel needs to run a program, and to stop and resume it:

```
struct proc
 ├── pid, name, parent             bookkeeping
 ├── pd                            its address space (lesson 10's page directory)
 ├── brk_start, brk                where its heap ends
 ├── exit_status                   for the parent's wait() (lesson 13)
 └── (its CPU registers)           the trap frame on the kernel stack
```

V7 kept processes in a fixed array, `proc[NPROC]`, and so do we
([`proc.c`](code/kernel/proc/proc.c)): finding a free slot is a loop
over the array, and a "pointer to a process" is never dangling.

### Where are a process's registers?

While a process runs in user mode, its registers are in the CPU. When it
enters the kernel (syscall, interrupt, fault), the trap frame holds them, at
the **top of the kernel stack**: the CPU starts pushing at `tss.esp0`, so the
full `struct trapframe` always ends exactly there:

```c
struct trapframe *proc_trapframe(void)
{
    return (struct trapframe *)(kstack_top - sizeof(struct trapframe));
}
```

Until lesson 23 only one process runs at a time, so **one kernel stack is
enough**. Changing what the process will do when it returns to user mode =
editing that trap frame. `exec` does exactly that.

## 2. exec: replace the program

`exec(path, argv, envp)` keeps the process (pid, parent, open files later)
and replaces the **program**: its address space and registers.

```
 1. find the file                        bootmod_find("hello")        (lesson 17: namei)
 2. check the ELF header                 magic, 32-bit, i386, executable
 3. NEW page directory                   vmm_new_pd()
 4. each PT_LOAD segment:                uvm_alloc + copy file bytes; .bss stays zero
 5. stack: 16 pages under 0xC0000000     uvm_alloc
 6. argv/envp onto the new stack         setup_stack
 ────────────── nothing can fail after this line ──────────────
 7. current->pd = new; free the old one; load CR3
 8. rewrite the trap frame               eip = entry, esp = new stack top, regs = 0
```

Steps 1–6 can fail (file missing, bad ELF, out of memory). Unix says a
failed exec **returns an error to the old program**, which must still be
intact. So build the new address space completely on the side, and only
switch when nothing can go wrong anymore.

### ELF in five minutes

An executable starts with a header pointing at an array of **program
headers**; each says "copy `filesz` bytes from file offset `offset` to
address `vaddr`, and make the region `memsz` bytes long (zero the rest)".
That's all a loader needs: the section headers (`.text`, `.data`) are for
linkers and debuggers.

```sh
$ i686-elf-readelf -lW build/user/bin/init
  Type  Offset   VirtAddr   FileSiz MemSiz  Flg
  LOAD  0x001000 0x00400000 0x0093c 0x0093c R E     ← .text + .rodata
  LOAD  0x001000 0x00401000 0x00000 0x00008 RW      ← .data + .bss (memsz > filesz)
```

From [`exec.c`](code/kernel/proc/exec.c):

```c
uint32_t flags = PTE_U | ((ph.flags & PF_W) ? PTE_W : 0);
if (uvm_alloc(pd, ph.vaddr, seg_end, flags) < 0) ...
for (uint32_t done = 0; done < ph.filesz; ) {       /* copy in 1 KiB chunks */
    ...
    read_image(img, ph.offset + done, buf, n);
    uvm_copy_to(pd, ph.vaddr + done, buf, n);
    done += n;
}
```

Validate everything: `vaddr` must be in user space, `vaddr + memsz` must not
wrap or reach the stack, `filesz ≤ memsz`. **An executable is user input.**

### Writing into an address space that isn't the current one

The new page directory isn't in CR3 yet, so the kernel can't just write to
`0x00400000`. `uvm_copy_to()` translates each page in the *target* directory
and writes to the frame through the direct map:

```c
while (n > 0) {
    uint32_t pa;
    if (vmm_translate(pd, va, &pa) < 0)
        return -1;
    uint32_t chunk = PAGE_SIZE - (va % PAGE_SIZE);   /* up to the end of this page */
    if (chunk > n)
        chunk = n;
    memcpy(P2V(pa), s, chunk);
    ...
}
```

The direct map pays off again; `fork` (lesson 13) uses the same idea.

### The initial stack (V7 layout)

```
 0xC0000000 ┌──────────────────────────┐ USER_STACK_TOP
            │ "USER=root\0"            │
            │ "HOME=/\0"               │  the strings
            │ "two\0" "one\0" "hello\0"│
            ├──────────────────────────┤ (aligned to 4)
            │ NULL                     │
            │ envp[1] ─────────────────┼──► "USER=root"
            │ envp[0]                  │
            │ NULL                     │
            │ argv[2] ...              │
            │ argv[0] ─────────────────┼──► "hello"
 ESP ─────► │ argc = 3                 │
            └──────────────────────────┘
```

This isn't a C call frame (there's no return address). `crt0.asm`, the
first code in every program, turns it into a call to `main`:

```nasm
_start:
    mov eax, [esp]              ; argc
    lea ebx, [esp + 4]          ; argv
    lea ecx, [ebx + eax*4 + 4]  ; envp: just past argv's NULL
    mov [environ], ecx
    push ecx
    push ebx
    push eax
    call main
    push eax
    call exit
```

### Copying arguments out of the old program first

`exec`'s arguments live in the **old** address space, which step 7 destroys.
So `sys_exece` first copies the path and every string into one kernel page
([`sys_proc.c`](code/kernel/sys/sys_proc.c), `struct exec_args`), checking
each user pointer (`user_ok`) and each string (`fetch_string`, which checks
each page it steps into). Only then does it call `exec()` with kernel strings.
`proc_start_init` calls the same `exec()` with strings that were in the
kernel to begin with.

## 3. The heap: `brk`

V7's memory model is simple: the program's data ends at the **break**, and
`brk(addr)` moves it. `sbrk(n)` (in libc) is "move the break by n, return the
old one", which is what `malloc` builds on.

```
 0x00400000  .text .rodata | .data .bss | heap ──► brk          ...  stack ◄── 0xC0000000
                                        ^ brk_start = end of the program (`end` symbol)
```

`sys_brk` maps fresh pages when the break grows and frees them when it
shrinks. `sbrk` keeps the current break in a libc variable that starts at the
linker symbol `end`, exactly like V7's `sbrk.s`.

## 4. When a program crashes

A fault from ring 3 must not panic the kernel. `trap()` now kills the
process: `proc_exit(signal)`, where the signal says why (V7 numbers in
[`abi/signal.h`](code/abi/signal.h): page fault/GP → `SIGSEGV`, divide →
`SIGFPE`, others → `SIGILL`).

**The exit status** follows V7's `wait()` format, so the shell can tell
"exited with code 3" from "killed by signal 11":

```
 exit(n)            → status = n << 8        (exit(3) = 0x0300)
 killed by signal s → status = s             (SIGSEGV = 0x000b)
```

`proc_exit` frees the address space (after switching CR3 to `kernel_pd`:
never free the page directory you're running on!) and marks the process
`ZOMBIE`. With no parent to return to yet, it halts. That's lesson 13.

## 5. User space

### The build

User programs are compiled by the same cross-compiler but against **our**
headers only: `-nostdinc -isystem <gcc's include dir>` keeps the compiler's
`stdarg.h`/`stddef.h`/`stdint.h` and nothing else. Each `user/bin/NAME.c` is
linked with `crt0.o` + `libc.a` into `build/user/bin/NAME`.

QEMU loads them as **Multiboot modules**: `-initrd "build/user/bin/hello,build/user/bin/init"`.
The loader puts each file in memory after the kernel and lists them in the
boot info (`mods_addr`). [`bootmod.c`](code/kernel/proc/bootmod.c) records
their names. It must run **before** `pmm_init`, which reserves the modules'
memory but not the list describing them. That's our only "filesystem" until
lesson 16.

### The link script

[`user/user.ld`](code/user/user.ld) puts code at `0x00400000` (page 0 stays
unmapped so NULL dereferences fault), starts writable data on a new page
so code can be read-only, and defines `end` for `sbrk`.

### libc, version 0

[`user/libc/`](code/user/libc/): `crt0.asm`, one C function per system call,
`sbrk`, `exit`, a few string functions, and a tiny `printf`. Lesson 20
replaces most of it with V7's own libc. Every syscall goes through one
helper that implements the ABI from lesson 11:

```c
static int syscall3(int num, int a, int b, int c)
{
    int ret;
    char failed;
    __asm__ volatile ("int $0x80"
                      : "=a"(ret), "=@ccc"(failed)   /* EAX, and the carry flag */
                      : "a"(num), "b"(a), "c"(b), "d"(c)
                      : "memory");
    if (failed) {
        errno = ret;
        return -1;
    }
    return ret;
}
```

---

## 6. Modules

| File | Responsibility | Exposes |
|---|---|---|
| [`proc/proc.c`](code/kernel/proc/proc.c) | process table, kernel stack, first process, exit | `current`, `proc_init`, `proc_start_init(path)`, `proc_trapframe()`, `proc_exit(status)` |
| [`proc/exec.c`](code/kernel/proc/exec.c) | ELF loading, stack setup, swapping the program | `exec(path, argv, envp)` (kernel strings) |
| [`proc/bootmod.c`](code/kernel/proc/bootmod.c) | Multiboot modules as named files | `bootmod_init`, `bootmod_find(name, &data, &size)` |
| [`mm/uvm.c`](code/kernel/mm/uvm.c) | user address-space operations | `uvm_alloc uvm_dealloc uvm_copy_to uvm_free`, `USER_BASE USER_STACK_TOP` |
| [`sys/sys_proc.c`](code/kernel/sys/sys_proc.c) | `exit getpid brk exec exece` | |
| [`sys/syscall.c`](code/kernel/sys/syscall.c) | `user_ok` now checks `current->pd`; `fetch_string` | |
| [`arch/i386/trap.c`](code/kernel/arch/i386/trap.c) | user faults → `proc_exit(signal)` | |

`vmm.c` stays about page tables; `uvm.c` is about *user* memory policy
built on top of it. Lesson 13 adds `uvm_clone` (for fork) there.

## 7. Test

- The milestone.
- Make `hello` dereference NULL: `pid 1 (hello) killed`, status `b`.
- Make `hello` write to a string literal (`.rodata`, read-only):
  `protection violation, write, user mode`.
- `exec` a module that isn't ELF (add a text file to `-initrd`):
  `ENOEXEC`, and the caller keeps running.
- `i686-elf-objdump -d build/user/bin/hello | less`: find `_start`, `main`,
  the `int $0x80`s.

## Pitfalls

- **`.data ALIGN(4K)` with an empty `.data`.** The linker drops the empty
  section *and its alignment*, so `.bss` lands in the read-only code page
  and the first write to a global kills the program. (This happened while
  writing the reference: `init` had no initialized data.) Use a separate
  `. = ALIGN(4K);`.
- **Freeing the current page directory** (switch CR3 first).
- **Copying argv after destroying the old address space.**
- **Forgetting the 4-byte alignment** of the stack pointer; or putting
  pointers to *kernel* copies of the strings on the user stack.
- **Calling `bootmod_init` after `pmm_init`** and finding the module list
  overwritten.
- **Linking without `-nostdlib`/`-nostdinc`**: the toolchain has no libc,
  so you'd get cryptic errors about `crt0.o` or `stdio.h`.

## References

- ELF: `man 5 elf` (Linux), or the "System V ABI, Intel386 supplement".
- V7: `usr/sys/sys/sys1.c` (`exec`, `getxfile`: the a.out version of all this),
  `usr/src/libc/csu/crt0.s`, `usr/src/libc/sys/sbrk.s`.
- xv6: `exec.c`, `proc.c` (`userinit`, `allocproc`), `initcode.S`, `ulib.c`, `usys.S`.
