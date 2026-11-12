# toyos tutorial — a V7-compatible Unix for i386, in C

Twenty-seven lessons take you from a kernel that prints "Hello" to a small
Unix that runs **Version 7 Unix's own programs**, compiled from their 1979
source: `ls cat cp mv rm ed grep sort find od tr su passwd`... It has a
shell with pipes and background jobs, ext2 on disk, users and permissions,
a preemptive scheduler and signals.

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
$ su ken
$ echo hi > /etc/passwd
sh: cannot create /etc/passwd
```

Target: **32-bit x86** (i386, protected mode, paging), booted by
**Multiboot** (QEMU `-kernel`), written in **C** with a little NASM
assembly. You need C and x86 assembly; you don't need to have written a
kernel before.

---

## How the lessons work

Each lesson is a folder:

```
12-Processes and exec/
├── README.md       the lesson: concepts, modules, code walkthrough, test, pitfalls
└── code/           the complete reference OS at the END of this lesson
```

- **Every lesson ends with a runnable OS.** `cd code && make run` boots it
  and shows the lesson's milestone. Nothing is ever "broken for three
  lessons".
- **`code/` is a full snapshot**, not a diff: lesson 12's `code/` builds
  on its own. Compare two snapshots (`diff -ru 11-*/code 12-*/code`) to see
  exactly what a lesson adds.
- **The README walks through the important code** with snippets, and links
  every new or changed file. The less interesting parts (a struct
  definition, a header) are only linked.
- The README sections are always the same: **Goal and Milestone** (what you
  will see), the **concepts** with pictures, the **code**, the **modules**
  table (file, responsibility, what it exposes), a **Test** list (including
  ways to break it on purpose), **Pitfalls** (the bugs everyone writes) and
  **References** (V7 source, xv6, Intel manuals, OSDev).

Two ways to use it: write each lesson yourself from the README and use
`code/` to check or to unblock yourself; or read the code with the README
next to it, and do the Test section's experiments.

Lesson 0 sets up the toolchain (`i686-elf-gcc`, `nasm`, `qemu-system-i386`,
`e2fsprogs`) on macOS or Linux.

---

## Curriculum

### Part I — Booting

| # | Lesson | You end with |
|---|---|---|
| 0 | [Environment and Hello World](0-Environment%20and%20Hello%20World/) | A Multiboot kernel prints in QEMU |
| 1 | [Console Output](1-Console%20Output/) | `kprintf("%d %x %s")` on VGA and serial; `panic()` |
| 2 | [Boot Information and the Linker Script](2-Boot%20Information%20and%20the%20Linker%20Script/) | The RAM map and the kernel's own size |
| 3 | [The GDT](3-The%20GDT/) | Your own segments, kernel and user |

### Part II — Interrupts

| # | Lesson | You end with |
|---|---|---|
| 4 | [Interrupts and Exceptions](4-Interrupts%20and%20Exceptions/) | Divide by zero prints a register dump instead of rebooting |
| 5 | [Hardware Interrupts](5-Hardware%20Interrupts/) | A clock ticking 100 times a second; keys on screen |

### Part III — Memory

| # | Lesson | You end with |
|---|---|---|
| 6 | [Physical Memory Manager](6-Physical%20Memory%20Manager/) | Allocate and free 4 KiB frames |
| 7 | [Paging](7-Paging/) | Paging on, with page tables you built; page-fault reports |
| 8 | [Higher-Half Kernel](8-Higher-Half%20Kernel/) | The kernel at `0xC0000000`, all RAM directly mapped |
| 9 | [Kernel Heap](9-Kernel%20Heap/) | `kmalloc` / `kfree` |

### Part IV — User Mode and Processes

| # | Lesson | You end with |
|---|---|---|
| 10 | [User Mode](10-User%20Mode/) | Code running in ring 3, trapping back on faults |
| 11 | [System Calls](11-System%20Calls/) | `int 0x80` with V7 numbers: `write`, `exit`, `getpid` |
| 12 | [Processes and exec](12-Processes%20and%20exec/) | ELF programs with their own address space, a tiny libc |
| 13 | [fork, wait and exit](13-fork%2C%20wait%20and%20exit/) | `fork` (one runnable process at a time), zombies, V7 wait status |

### Part V — Disk and Filesystem

| # | Lesson | You end with |
|---|---|---|
| 14 | [ATA Disk Driver](14-ATA%20Disk%20Driver/) | Read and write disk sectors |
| 15 | [Buffer Cache](15-Buffer%20Cache/) | V7's `bread` / `bwrite` / `brelse`; the device switch |
| 16 | [Reading ext2](16-Reading%20ext2/) | Path lookup and file reading on an ext2 disk |
| 17 | [Files and File Descriptors](17-Files%20and%20File%20Descriptors/) | `open read close lseek dup stat chdir`; `exec` from disk; `/dev` |
| 18 | [The TTY](18-The%20TTY/) | A line-editing terminal; `ioctl` with V7's `sgtty` |
| 19 | [Writing ext2](19-Writing%20ext2/) | `creat write link unlink mknod chmod`; `e2fsck` stays clean |

### Part VI — User Space

| # | Lesson | You end with |
|---|---|---|
| 20 | [User-Space C Library](20-User-Space%20C%20Library/) | V7's own libc: stdio, `printf`, `malloc`, `ctime`, `crypt` |
| 21 | [A Simple Shell](21-A%20Simple%20Shell/) | `init` and a shell: `$PATH`, `< > >>`, `;`, quotes, scripts |
| 22 | [Porting V7 Programs](22-Porting%20V7%20Programs/) | 44 V7 commands; users, permissions, setuid |

### Part VII — Multitasking

| # | Lesson | You end with |
|---|---|---|
| 23 | [Kernel Stacks and the Scheduler](23-Kernel%20Stacks%20and%20the%20Scheduler/) | A kernel stack per process, `swtch`, real `fork`, `&` |
| 24 | [Sleep, Wakeup and Preemption](24-Sleep%2C%20Wakeup%20and%20Preemption/) | Waiting costs nothing; the clock preempts; FPU state saved |
| 25 | [Pipes](25-Pipes/) | `a \| b \| c`, `dup2`, `popen`, V7's `find` |
| 26 | [Signals](26-Signals/) | ^C, `kill`, `alarm`, handlers; V7's `sleep` |

Why multitasking comes **last**: until lesson 23, `fork` runs the child to
completion while the parent waits. That's enough for a shell, a filesystem
and 44 V7 programs, and it means there's one kernel stack and no
concurrency to think about while you learn everything else. Lessons 23–26
then add concurrency to a system that already works.

---

## The system at the end

You don't need to understand this section yet. Come back to it at each
lesson to see where the new piece fits.

### Layers

```
 user space (ring 3)   ┌───────────────────────────────────────────────────┐
                       │ V7 programs: ls cat ed grep sort find su ...      │
                       │ ours: init sh   tests: crash fpu sigtest          │
                       │ libc: V7's stdio/gen + our crt0, syscalls, printf │
                       └────────────────────────┬──────────────────────────┘
                    int 0x80 (V7 numbers)       │   ▲ iret
 ═══════════════════════════════════════════════╪═══╪═══════════════════════
 kernel (ring 0)                                ▼   │
   arch/i386: isr.asm ──► trap.c ──► sys/syscall.c ──► sys_file.c sys_proc.c
                            │   (signals delivered here    │
                            │    on the way back out)       │
             ┌──────────────┴────┐         ┌───────────────┼────────────────┐
             ▼                   ▼         ▼               ▼                ▼
       dev/ drivers         proc/          fs/ file.c pipe.c          mm/ uvm vmm
       console tty kbd      proc.c exec.c  dir.c inode.c perm.c       pmm kheap
       serial vga ata rtc   signal.c swtch super.c (ext2) bio.c
```

### Source tree (lesson 26)

```
code/
├── Makefile                build the kernel, libc, programs, disk image
├── abi/                    headers shared by kernel AND user space: the contract
│   ├── syscall.h           numbers (V7's), register conventions        L11
│   ├── errno.h types.h     V7 values and types                         L11, L16
│   ├── stat.h dir.h        struct stat, struct direct (V7 layout)      L16–17
│   ├── sgtty.h             terminal modes                              L18
│   └── signal.h            signal numbers                              L12
├── kernel/
│   ├── kmain.c             the boot sequence
│   ├── arch/i386/          boot.asm linker.ld gdt idt isr.asm trap.c pic pit fpu cpu.h
│   ├── lib/                kprintf panic string
│   ├── dev/                console vga serial kbd tty ata rtc dev.c (device switches)
│   ├── mm/                 pmm (frames) vmm (page tables) uvm (user memory) kheap
│   ├── fs/                 bio (buffer cache) super inode dir perm (ext2) file pipe
│   ├── proc/               proc.c (table, scheduler) exec.c signal.c swtch.asm
│   └── sys/                syscall.c (dispatch) sys_file.c sys_proc.c
├── user/
│   ├── user.ld             programs are linked at 0x00400000
│   ├── libc/               crt0 syscall.c printf scanf setjmp sigtramp ... + include/
│   │   └── v7/             V7's libc sources, nearly unchanged
│   ├── bin/                our programs: init sh, and tests
│   └── v7bin/              V7 commands, unchanged except a few lines of ed
├── root/etc/               passwd group motd rc: copied onto the disk
└── tools/mkdisk.sh         builds build/disk.img (ext2) with /bin /dev /etc /tmp /usr
```

**Why `abi/`?** Source compatibility with V7 means a V7 program's idea of
`struct stat`, `struct direct`, `struct sgttyb`, `errno` values and signal
numbers must match what the kernel produces. Each is defined once in
`abi/` and included from both sides, so they can never disagree.

### The system call ABI

| | |
|---|---|
| Entry | `int 0x80`: an interrupt gate with DPL 3, so ring 3 may use it |
| Number | EAX, **V7's numbers** (`write` = 4, `fork` = 2, `exece` = 59...) |
| Arguments | EBX, ECX, EDX, ESI, EDI |
| Result | EAX |
| Error | **carry flag set** and EAX = the errno value (V7's convention, on the PDP-11 too) |
| Second result | EDX: `pipe` (write fd), `getuid`/`getgid` (effective id) |
| `dup2` | `dup(fd \| 0100, fd2)`, as in V7 |

Directories are **ext2** on disk, but `read()` on a directory returns V7's
16-byte `struct direct` entries (and `stat` reports their size), so V7's
`ls`, `pwd` and `find` work unchanged.

### Memory maps

**Virtual address space of every process** (the top 1 GiB is identical in
all of them):

```
0xFFFFFFFF ┌──────────────────────────────┐
           │ (unused)                     │
0xD0000000 ├──────────────────────────────┤  KERNBASE + PHYSTOP
           │ direct map of ALL physical   │  virt = phys + 0xC0000000
           │ RAM: kernel image at         │  ── kernel only (U/S = 0)
           │ 0xC0100000, heap, page       │
           │ tables, kernel stacks...     │
0xC0000000 ├──────────────────────────────┤  KERNBASE
           │ user stack (64 KiB)          │  ── user (U/S = 1)
           │            ...               │
           │ heap (brk, grows up)         │
           │ .data .bss                   │
           │ .text                        │
0x00400000 ├──────────────────────────────┤  where user programs are linked
           │ unmapped: NULL-pointer trap  │
0x00000000 └──────────────────────────────┘
```

**Physical memory** (QEMU `-m 128M`):

```
0x00000000  real-mode IVT, BIOS data, Multiboot info   ─┐ never handed out
0x0009FC00  EBDA, VGA memory at 0xB8000, BIOS ROMs     ─┘
0x00100000  kernel image (.text .rodata .data .bss)  ← loaded here
kernel_end  free frames → the physical memory manager
0x08000000  end of RAM (128 MiB)
```

The **direct map** is the most important idea in the memory design: all
physical RAM also appears at `phys + 0xC0000000`, so the kernel can reach
*any* frame (a page table, a new process's memory, a disk buffer) by adding
a constant: `P2V()` and `V2P()`. Lesson 8 explains why this removes most
of the pain of paging.

### Boot sequence (`kmain`, lesson 26)

```
console_init                      VGA + serial, so we can print       L1
gdt_init                          segments, TSS                       L3, L10
fpu_init                          FPU on, fxsave allowed              L24
idt_init                          exceptions print instead of reboot  L4
pic_init pit_init                 hardware interrupts, the clock      L5
rtc_init                          the date, from the CMOS clock       L19
tty_init kbd_init serial input    the terminal                        L18
pmm_init vmm_init                 frames, the kernel page directory   L6–8
ata_init bio_init                 the disk, the buffer cache          L14–15
ext2_mount_root                   "/"                                 L16
proc_start_init("/bin/init")      process 1                           L12
scheduler()                       never returns                       L23
```

### A process's life

```
          fork()                exec(path)              exit(status)
 parent ───────────► child ───────────────► new image ───────────────► ZOMBIE
   │      (RUNNABLE ⇄ RUNNING ⇄ SLEEPING, until it exits)                 │
   └─────────── wait(&status) ◄── the parent collects pid + status ◄──────┘
```

---

## System calls, and the lesson that adds each

Numbers are V7's (`usr/sys/sys/sysent.c`).

| # | call | lesson | | # | call | lesson |
|---|---|---|---|---|---|---|
| 1 | exit | 11 | | 20 | getpid | 11 |
| 2 | fork | 13 (23: concurrent) | | 23/24 | setuid/getuid | 22 |
| 3 | read | 17 | | 25 | stime | 22 |
| 4 | write | 11 | | 27 | alarm | 26 |
| 5 | open | 17 | | 28 | fstat | 17 |
| 6 | close | 17 | | 29 | pause | 26 |
| 7 | wait | 13 | | 30 | utime | 22 |
| 8 | creat | 19 | | 33 | access | 22 |
| 9 | link | 19 | | 36 | sync | 19 |
| 10 | unlink | 19 | | 37 | kill | 26 |
| 11 | exec | 12 | | 41 | dup (and dup2) | 17 (25) |
| 12 | chdir | 17 | | 42 | pipe | 25 |
| 13 | time | 19 | | 46/47 | setgid/getgid | 22 |
| 14 | mknod | 19 | | 48 | signal | 26 |
| 15 | chmod | 19 | | 54 | ioctl | 18 |
| 16 | chown | 19 | | 59 | exece | 12 |
| 17 | break (brk) | 12 | | 60 | umask | 22 |
| 18 | stat | 17 | | | | |
| 19 | lseek | 17 | | | | |

Not implemented: `mount`/`umount` (one filesystem), `nice` and `times`
(no CPU accounting), `ptrace`, `profil`, `acct`, `chroot`, `lock`, `phys`.

---

## Where to go from here

What V7 had and toyos doesn't, roughly in order of difficulty:

- **Process groups and several terminals**: `getty`, `login`, `/etc/ttys`
  (lesson 26 explains the shortcut we took).
- **Core dumps** for the "bug" signals, and `adb` to read them.
- **An interrupt-driven disk**: sleep during I/O, with V7's `B_BUSY` and
  `ILOCK` to keep two processes out of the same buffer or inode (lesson 24).
- **Demand paging**: grow the stack and heap on page faults, and
  copy-on-write `fork`.
- **V7's Bourne shell**: possible with the system calls we have, but its
  source needs heavy rewriting for a modern compiler (lesson 26).
- **Booting from a disk image** with GRUB, or your own boot sector,
  instead of QEMU's `-kernel`.

---

## References you'll use throughout

- **V7 Unix source**: <https://www.tuhs.org/cgi-bin/utree.pl?file=V7>
  (browse) and <https://www.tuhs.org/Archive/Distributions/Research/>
  (download). Kernel in `usr/sys`, commands in `usr/src/cmd`, libc in
  `usr/src/libc`. Caldera's 2002 license: <https://www.tuhs.org/Archive/Caldera-license.pdf>.
- **xv6 (x86 version)**: <https://github.com/mit-pdos/xv6-public> and its
  book. A V6-like Unix for i386, the closest relative of toyos.
- **Lions' Commentary on UNIX 6th Edition**: V6's kernel line by line; V7
  is very close.
- **Intel SDM Vol. 3A** (System Programming Guide): ch. 3 segmentation,
  ch. 4 paging, ch. 6 interrupts, ch. 7 tasks/TSS.
- **OSDev wiki**: <https://wiki.osdev.org/>.
- **Multiboot spec**: <https://www.gnu.org/software/grub/manual/multiboot/multiboot.html>.
- **ext2**: <https://www.nongnu.org/ext2-doc/ext2.html>.
