# toyos

> [!NOTE]
> **This repository was written 100% by Claude Opus 5.5 (Anthropic).**
> That covers the tutorial, every line of the reference implementation and
> this README.
> The `kernel/` folder at the root is different: that's where I write my
> own OS by hand while following the tutorial.

A tutorial in 27 lessons for writing a small **Unix-like operating system in
C for 32-bit x86**, plus a complete, working reference implementation. The
finished OS runs **Version 7 Unix's own programs** (`ls`, `cat`, `ed`,
`grep`, `sort`, `find`, `su`... 44 of them), compiled unchanged from their
1979 source.

```
$ ls -l /bin | grep rws | wc -l          <- three processes, two pipes
      5
$ cat /etc/passwd | sort | tr a-z A-Z
BIN::3:1::/BIN:
KEN::100:10:KEN THOMPSON:/USR/KEN:/BIN/SH
ROOT::0:1:THE SUPERUSER:/:/BIN/SH
$ sleep 100 &
5
$ yes > /dev/null
                                          <- ^C kills yes, not the sleep
```

> [!WARNING]
> **The tutorial is not complete yet.** The reference implementation works
> for all 27 lessons: every lesson's `code/` folder builds and boots, and
> the last one runs everything shown above. But only **lessons 0 to 4**
> have a detailed, step-by-step writeup that teaches the concepts well
> enough to write the code yourself. **Lessons 5 to 26 are still crude:**
> their READMEs are short summaries of the code, closer to an index than a
> tutorial. They are being rewritten one at a time.
>
> | Lessons | Code | Writeup |
> |---|---|---|
> | 0–4 (booting, console, boot information, GDT, interrupts) | works | detailed tutorial |
> | 5–26 | works | crude: summary of the code |

## Why this exists

I've always wanted to write an operating system, for no reason except to
understand how one works. I tried before, and every time I ran into the same
problems with the guides I found:

- **Most of them stop early.** You get a kernel that boots and prints text,
  maybe a keyboard driver, and then the guide just ends, right before the
  parts that are actually hard.
- **They skip the concepts I struggled with most:** interrupt handling,
  system calls and a proper virtual memory setup (page tables, a
  higher-half kernel, separate address spaces per process). These get a
  paragraph and a code dump, when they're exactly what needs to be explained
  slowly.
- **Many don't target x86**, or target x86-64 only. 32-bit x86 is the only
  hardware platform I had the chance to study properly during my bachelor's
  degree, and I wanted to build on that.
- **Many newer ones use Rust.** I know C, and I wanted to spend my effort on
  the OS, not on learning a language at the same time.

So I asked Claude for help. Claude can write a whole OS on its own (it
did: that's the reference implementation in this repository), but having
an AI write an OS teaches me nothing. What I asked for instead was a
**tutorial**: lessons that walk me through each concept, with concrete
pictures (stack layouts, worked address translations with real numbers) so
that **I can write the code myself**. The reference code is there to check
my work against, or to get unstuck, not to copy.

## What's inside

```
tutorial/
  README.md              the index: curriculum, architecture, ABI, memory maps
  0-Environment.../      lesson 0
    README.md            the lesson
    code/                the complete, runnable OS at the end of that lesson
  ...
  26-Signals/
kernel/, Makefile        my own OS, written by hand (work in progress)
AGENTS.md                instructions for AI assistants working in this repo
```

Each lesson's `code/` is a **full snapshot**, not a diff, and **every lesson
ends with an OS that boots**. To see exactly what a lesson adds, diff two
neighbouring snapshots:

```sh
diff -ru "tutorial/11-System Calls/code" "tutorial/12-Processes and exec/code"
```

A finished lesson README (lessons 0 to 4 so far) explains each concept from
scratch, with worked examples using real numbers from the running kernel,
then guides you through writing the code in small steps that each build and
run. The reference code for each step is in a collapsed block, so you can
try first. Each lesson ends with exercises (many of them about breaking
things on purpose and reading the result), pitfalls, and references.

### The curriculum

| Part | Lessons | You end with |
|---|---|---|
| Booting | 0–3 | A Multiboot kernel, `kprintf` on VGA and serial, the memory map, your own GDT |
| Interrupts | 4–5 | Exceptions print a register dump instead of rebooting; a ticking clock; a keyboard |
| Memory | 6–9 | A frame allocator, paging, a higher-half kernel with all RAM directly mapped, `kmalloc` |
| User mode and processes | 10–13 | Ring 3, `int 0x80` system calls (V7's numbers), ELF `exec`, `fork`/`wait`/`exit` |
| Disk and filesystem | 14–19 | An ATA driver, V7's buffer cache, reading and writing ext2, file descriptors, a TTY |
| User space | 20–22 | V7's own libc, a shell with redirection and scripts, 44 ported V7 commands, users and permissions |
| Multitasking | 23–26 | Per-process kernel stacks, a preemptive scheduler, sleep/wakeup, pipes, signals |

Multitasking comes **last** on purpose: until lesson 23, `fork` runs the
child to completion while the parent waits. That's enough for a shell, a
filesystem and dozens of programs, with no concurrency to think about while
you learn everything else.

See [`tutorial/README.md`](tutorial/README.md) for the full lesson list,
the architecture diagrams, the system call table and the memory maps.

### The target

- **32-bit x86** (i386): protected mode, two-level paging, a higher-half
  kernel at `0xC0000000` with a direct map of physical RAM.
- **Multiboot** boot, loaded directly by QEMU (`-kernel`).
- **C** with a little NASM assembly (Intel syntax).
- **Source-compatible with Version 7 Unix**: V7's system call numbers and
  conventions, and its `struct stat`, `struct direct`, `sgtty`, errno and
  signal values, shared between kernel and libc in `abi/`.
- VGA text console and serial port, ext2 filesystem on an emulated IDE disk.

You need to know C and some x86 assembly. You don't need to have written a
kernel before.

## Building and running on Apple Silicon

Everything runs natively on an Apple Silicon Mac: the code is
cross-compiled for i386 and runs in QEMU. (The same packages exist on Linux
and Intel Macs; lesson 0 covers the setup in more detail.)

### 1. Install the toolchain

```sh
brew install i686-elf-gcc nasm i386-elf-gdb e2fsprogs qemu
```

| Package | Used for |
|---|---|
| `i686-elf-gcc` | A cross-compiler for bare-metal i386 (plus `i686-elf-ld`, `objdump`, `addr2line`) |
| `nasm` | The assembler for the boot code and interrupt stubs |
| `qemu` | `qemu-system-i386`, the emulated PC |
| `i386-elf-gdb` | Debugging the kernel through QEMU's GDB stub |
| `e2fsprogs` | `mke2fs`, `debugfs`, `e2fsck`: building and checking the ext2 disk image (lesson 16 on) |

Don't use macOS's own `clang`/`gcc`: they produce Mach-O binaries for the
host, not ELF for a bare i386 machine.

`e2fsprogs` is "keg-only" in Homebrew, so its tools are not on your `PATH`.
The build script finds them in `/opt/homebrew/opt/e2fsprogs/sbin` by
itself; to run `e2fsck` by hand, use the full path or add it to your `PATH`:

```sh
export PATH="/opt/homebrew/opt/e2fsprogs/sbin:$PATH"
```

### 2. Build and boot the reference OS

Pick any lesson and run `make` in its `code/` folder. The last lesson is
the complete system:

```sh
cd "tutorial/26-Signals/code"
make            # kernel, libc, user programs and build/disk.img
make run        # QEMU window, plus the serial console in this terminal
```

You should see:

```
toyos booting...
pmm: 32436 frames free (126 MiB)
vmm: kernel page directory at 0012c000
ata: "QEMU HARDDISK", 16384 sectors (8192 KiB)
ext2: 8192 blocks, 2048 inodes, 1 group(s), 4194 free blocks
starting /bin/init as pid 1
Welcome to toyos!
$
```

Type commands either in the QEMU window or in the terminal. Try `ls -l
/bin`, `cat /etc/passwd | sort`, `ed`, `su ken`, `sleep 100 &`, or `yes >
/dev/null` followed by `^C`.

### Other `make` targets

| Command | What it does |
|---|---|
| `make` | Build everything |
| `make run` | QEMU window + serial console on the terminal |
| `make run-nox` | Serial console only, no window (quit with `Ctrl-A` then `X`) |
| `make run-test` | Headless, serial on stdout, for scripted tests |
| `make debug` then `make gdb` (second terminal) | QEMU paused with a GDB stub on port 1234, and GDB attached to it |
| `make clean` | Remove `build/` |

The disk image `build/disk.img` persists between runs, so files you create
are still there next time. Delete it (`rm build/disk.img`) to get a fresh
one on the next `make`. After a session that writes files, check that the
filesystem is still consistent:

```sh
/opt/homebrew/opt/e2fsprogs/sbin/e2fsck -fn build/disk.img
```

Commands can also be scripted over the serial port (`\r` is Enter):

```sh
(sleep 2; printf 'ls -l /bin | wc\r'; sleep 1) | timeout 5 make -s run-test
```

### Following along

Start at [lesson 0](tutorial/0-Environment%20and%20Hello%20World/), which
sets up the environment and boots a "Hello World" kernel. The root
`Makefile` and `kernel/` folder are a lesson-0 starting point with the same
`make` targets, ready for you to replace with your own work (or copy them
into a fresh repository). Two ways to use the lessons:

1. **Write each lesson yourself** from its README, and use `code/` to check
   your work or to get unstuck.
2. **Read the code** with the README next to it, and do the Test section's
   experiments.

## What it doesn't do

Left out on purpose, and discussed in the lessons: V7's Bourne shell (its
source needs heavy rewriting for a modern compiler, so toyos has its own
small shell), an interrupt-driven disk, process groups and `login`, core
dumps, demand paging and copy-on-write `fork`, and booting from a disk image
instead of QEMU's `-kernel`.

## References

- **V7 Unix source**: <https://www.tuhs.org/cgi-bin/utree.pl?file=V7>
- **xv6** (x86 version), the closest relative of toyos: <https://github.com/mit-pdos/xv6-public>
- **Lions' Commentary on UNIX 6th Edition**
- **Intel SDM Vol. 3A** (System Programming Guide)
- **OSDev wiki**: <https://wiki.osdev.org/>

## License note

The V7 Unix source code used in the reference implementation (the C library
in `user/libc/v7/` and the commands in `user/v7bin/`) is covered by
[Caldera's 2002 BSD-style license](https://www.tuhs.org/Archive/Caldera-license.pdf).
Changes to that code are marked with `toyos:` comments.
