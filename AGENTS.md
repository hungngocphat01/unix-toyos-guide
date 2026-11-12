# AGENTS.md — toyos

Read this file completely before doing anything in this repository.

## What this project is

A **learning project** with two parts:

1. **`tutorial/`**: a 27-lesson curriculum (0–26) for writing a small
   Unix-like OS in C, with a **reference implementation**: every lesson
   folder holds a `README.md` and a `code/` directory containing the
   complete, runnable OS as it is at the end of that lesson.
2. **`kernel/`, `Makefile` at the root**: the owner's **own** OS, written
   by hand while following the tutorial. The point is for **them** to
   understand and write it.

### Target

- **Source-compatible with Version 7 Unix (1979)**: V7's programs and libc
  compile against our headers and run on our kernel.
- **32-bit x86 (i386)**, protected mode, two-level paging, higher-half kernel
  at `0xC0000000` with a direct map of physical RAM.
- **Multiboot** boot (QEMU `-kernel`).
- Text console (VGA text mode + serial), ext2 filesystem, a simple shell,
  multitasking last (lessons 23–26).

### About the owner

- Knows C and **x86 assembly (Intel syntax)**. Does **not** know Rust or ARM.
- Has tried writing an OS before and struggled with **interrupts, system calls
  and virtual memory**. Knows the theory of page tables but finds it
  unintuitive. Explain these with concrete pictures (stack layouts, worked
  address translations), not just definitions.
- The tutorial's code is read by a human learner: it must be **clear,
  succinct and easy to follow** (comments explain *why*; no cleverness).

## Rules for AI assistants

### When helping the owner with *their* OS (`kernel/`, root `Makefile`)

1. **Do not write their kernel or user-space code.** No function bodies, no
   patches, even if asked casually. Instead:
   - explain the concept (diagrams, worked examples with real numbers);
   - say **which module** should own the job, what it exposes, what it
     depends on;
   - point at the hardware fact (register, bit, port, table layout) and a
     reference (Intel SDM, OSDev, V7/xv6 file, or the lesson's `code/`);
   - when debugging, help them *find* the bug: what to print, which QEMU/GDB
     command to run, which assumption to check. Name the suspected line and
     explain why it's wrong; let them write the fix.
   - Tiny snippets (≤ 3 lines) are fine only for syntax questions (inline
     asm constraints, a NASM directive).
   - Exception: if the owner explicitly says "write this for me" for a
     specific piece, confirm, then do only that piece.
2. **The owner decides structure, names, signatures and the ABI** of their
   OS. Adapt your guidance to their design; don't steer them back to the
   tutorial's names.
3. Keep them on a path where **every step boots** (`make run`).
4. Before answering "why doesn't it work", **read their current code**.
   Don't guess from the tutorial.

### When working on the tutorial (`tutorial/`)

5. **Read [`tutorial/FEEDBACK.md`](tutorial/FEEDBACK.md) first.** It holds
   the owner's feedback on the lessons and the checklist every lesson
   README must pass. When the owner gives new feedback about the tutorial,
   add it there (what they said, how you interpreted it, what to avoid).
6. The tutorial **is** yours to write and fix: lesson READMEs, the
   reference code in `tutorial/*/code/`, `tutorial/README.md`, this file.
7. **Every claim in a README must be verified against the lesson's `code/`**:
   build it, boot it, run the milestone and the tests. Quote real output.
8. **Each `code/` is a full snapshot**, not a diff. A fix to code that
   several lessons share must be applied to **every** later snapshot that
   contains it (and rebuilt).
9. Lesson README shape: what you'll build and see, then the teaching
   itself in steps (concept first, then the task, a check, and the
   reference code collapsed), then Exercises, Pitfalls, References. Code
   comments explain why; V7 names where V7 had the concept.
10. Code taken from V7 (`user/libc/v7/`, `user/v7bin/`) stays as close to the
   original as possible; every change is marked with a `toyos:` comment.

## Toolchain and commands (macOS, Apple Silicon host)

Homebrew: `i686-elf-gcc` (+ binutils: `i686-elf-objdump`, `i686-elf-addr2line`...),
`nasm`, `i386-elf-gdb`, `qemu-system-i386`, `e2fsprogs` (keg-only: tools in
`/opt/homebrew/opt/e2fsprogs/sbin`: `mke2fs`, `debugfs`, `e2fsck`).

In any lesson's `code/` (from lesson 16 on, `make` also builds user
programs and `build/disk.img`):

| Command | What it does |
|---|---|
| `make` | kernel, libc, programs, disk image |
| `make run` | QEMU window + serial on the terminal |
| `make run-nox` | serial only (quit: `Ctrl-A` then `X`) |
| `make run-test` | headless, serial on stdout: for scripted tests |
| `make debug` + `make gdb` | QEMU paused with a GDB stub on :1234 |
| `make clean` | removes `build/` |

Scripted test pattern (type commands over the serial port; `\r` = Enter,
`\003` = ^C, `\034` = ^\, `\020` = ^P process list):

```sh
(sleep 2; printf 'ls -l /bin | wc\r'; sleep 1) | timeout 5 make -s run-test
```

After any session that writes files: `e2fsck -fn build/disk.img` must be
clean. The disk image persists between runs; `rm build/disk.img` for a fresh
one. Fault addresses: `i686-elf-addr2line -f -e build/user/bin/PROG <eip>`.

## Repository layout

```
Makefile, kernel/   the owner's own OS (lesson-0 barebone + their work)
tutorial/
  README.md         index, architecture, ABI, syscall table
  N-Name/README.md  lesson N
  N-Name/code/      the reference OS at the end of lesson N
```

## Decisions record (the reference implementation's ABI and design)

- **Boot**: Multiboot v1, kernel loaded at 1 MiB, runs at `0xC0100000`.
- **System calls**: `int 0x80` (interrupt gate, DPL 3); **V7's numbers**
  (`abi/syscall.h`); arguments in EBX, ECX, EDX, ESI, EDI; result in EAX;
  **error = carry flag set and EAX = errno** (V7's convention); a second
  result in **EDX** (`pipe`: write fd; `getuid`/`getgid`: effective id);
  `dup2` is `dup(fd | 0100, fd2)` as in V7.
- **User programs**: ELF, linked at `0x00400000`; stack 64 KiB below
  `0xC0000000`; `brk` heap after `.bss`.
- **Headers shared by kernel and libc**: `abi/` (errno, types, stat, dir,
  sgtty, signal, syscall), V7 layouts and values.
- **Filesystem**: ext2 rev 1, 1 KiB blocks, 128-byte inodes, only the
  `filetype` feature; write-through buffer cache. `read()` on a directory
  returns V7 16-byte `struct direct` entries, and `stat` reports a
  directory's size in that view (16 bytes per entry).
- **Devices**: `bdevsw` (ATA) and `cdevsw` (0 = console tty, 1 = null);
  device numbers in the inode's `block[0]` (old format) or `block[1]`.
- **Processes**: until lesson 22, sequential `fork` on one kernel stack;
  from lesson 23, a kernel stack per process, `swtch`, round-robin
  scheduler; preemption only from user mode (the kernel is not
  preemptible; interrupts are off in kernel code); FPU saved with
  `fxsave`/`fxrstor` on each switch; kernel built `-mgeneral-regs-only`.
- **Signals**: V7 semantics (one-shot handlers, all default actions exit,
  `exec` keeps ignored signals). The kernel pushes `eip, eflags, sig` on the
  user stack and jumps to libc's `_sigtramp`; no `sigreturn`. No process
  groups: terminal signals and `kill(0, …)` reach every process but init.
- **libc**: V7's `usr/src/libc` C files compiled `-std=gnu89 -w -fcommon`,
  plus our own `crt0`, system call stubs, `_doprnt`, `setjmp`,
  `sigtramp`.
- **Not done, on purpose** (documented in the lessons): V7's Bourne shell
  (its source needs heavy rewriting), interrupt-driven disk with
  buffer/inode locks, process groups/login, core dumps, demand paging,
  booting from a disk image.

## Progress

| Part | Lessons | Tutorial + reference code |
|---|---|---|
| Booting | 0–3 | done |
| Interrupts | 4–5 | done |
| Memory | 6–9 | done |
| User mode and processes | 10–13 | done |
| Disk and filesystem | 14–19 | done |
| User space | 20–22 | done |
| Multitasking | 23–26 | done |
| Booting from a disk image | (27, optional) | not written |

Lesson READMEs rewritten as real tutorials (see `tutorial/FEEDBACK.md`):
0–4. The others are still in the old, index-like style. When you finish
rewriting a lesson, update this line AND the status warning at the top of
the root `README.md` (the lesson range in the text and the table).

The owner's own OS (`kernel/`): lesson 0 barebone. Update this line as they
progress.
