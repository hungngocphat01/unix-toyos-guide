# 0 — Environment and Hello World

**Goal:** understand every file of the barebone kernel that's already in the
repo, and be able to build, run and debug it.

**Milestone:** `make run` opens a QEMU window showing
`Hello, world from toyos!` and prints the same line in your terminal.

**Reference code:** [`code/`](code/) is the complete codebase for this lesson
(identical to the starting point at the repo root). Every lesson has such a
`code/` folder: build any of them with `make -C "<lesson>/code" run`.

---

## 1. The toolchain, and why each piece exists

You're on a Mac with an ARM CPU, building an OS for an x86 CPU that doesn't
exist on your desk. So **everything is "cross"**:

| Tool | Why |
|---|---|
| `i686-elf-gcc` | A C compiler that emits **32-bit x86** code in **ELF** format, and assumes **no operating system** (no libc, no Linux syscalls). Your Mac's `clang` targets macOS/arm64 and would quietly add assumptions about Darwin. |
| `i686-elf-ld`, `objdump`, `readelf`, `nm`, `addr2line` | binutils for the same target. `objdump -d` and `addr2line` are your best friends when a crash prints an address. |
| `nasm` | Assembler for the few parts C can't do (entry point, interrupt stubs, context switch). Intel syntax, which you already know. |
| `qemu-system-i386` | An emulated PC. Reboots in a second, can't be bricked, has a GDB stub and an interrupt logger. |
| `i386-elf-gdb` | Debugger that understands 32-bit x86 ELF. Attaches to QEMU. |
| `e2fsprogs` | `mke2fs`, `debugfs`, `e2fsck` to build and check ext2 disk images (lesson 16+). Keg-only: they're in `/opt/homebrew/opt/e2fsprogs/sbin`. |

Installed with:

```sh
brew install i686-elf-gcc nasm i386-elf-gdb e2fsprogs qemu
```

### What `-ffreestanding` really means

A *hosted* C program can assume `printf`, `malloc`, `main`, a C runtime that
zeroes `.bss`, and an OS underneath. A *freestanding* one can only assume
`<stdint.h>`, `<stddef.h>`, `<stdarg.h>`, `<stdbool.h>`, `<limits.h>`,
`<float.h>`, `<iso646.h>` and `<stdalign.h>`. Everything else you write
yourself. That's the whole kernel's world.

One thing GCC may still do: emit calls to `memcpy`, `memset`, `memmove`,
`memcmp` for struct copies or big initializers, **even if you never call
them**. So you'll write those four first (lesson 1). Also `-lgcc` is linked
in for helpers like 64-bit division (`__udivdi3`).

The other flags in the Makefile:

| Flag | Why |
|---|---|
| `-fno-stack-protector` | Stack canaries need a runtime (`__stack_chk_fail`) you don't have. |
| `-fno-pic` | The kernel is at a fixed address; position-independent code just wastes a register. |
| `-O2 -g` | Optimize but keep debug info. If a bug "disappears at `-O0`", you have undefined behavior or a missing `volatile`. |
| `-nostdlib` (link) | Don't link crt0, libc. |

---

## 2. The files

| File | Role |
|---|---|
| [`Makefile`](code/Makefile) | builds every `.c`/`.asm` under `kernel/` |
| [`kernel/arch/i386/boot.asm`](code/kernel/arch/i386/boot.asm) | Multiboot header + `_start` |
| [`kernel/arch/i386/linker.ld`](code/kernel/arch/i386/linker.ld) | memory layout of the kernel image |
| [`kernel/kmain.c`](code/kernel/kmain.c) | first C code |

### `boot.asm` — from the bootloader to C

How QEMU's `-kernel` option (or GRUB) boots you:

1. It scans the first 8 KiB of your file for the **Multiboot header**: three
   32-bit words `magic, flags, checksum` where `magic + flags + checksum == 0`.
2. It loads every ELF segment of `kernel.elf` at its physical address (1 MiB
   for us; see the linker script).
3. It switches the CPU into **32-bit protected mode with paging off** and
   jumps to the ELF entry point (`_start`), with `EAX = 0x2BADB002` and
   `EBX = address of the Multiboot info struct`.

The CPU state at `_start`, which matters later:

- CS/DS/ES/SS are *some* flat 4 GiB segments set up by the loader. **You don't
  own that GDT and must not rely on it** (lesson 3 installs yours).
- Interrupts are disabled (`IF=0`). There's no IDT. If anything faults now,
  the CPU **triple-faults and resets**: QEMU restarts or (with
  `-no-reboot`) quits. This is the "my OS reboots in a loop" symptom.
- **ESP is garbage.** C needs a stack, so the first instruction sets
  `esp = stack_top`. The stack is 16 KiB reserved in `.bss`. Stacks grow
  *down*, which is why we load the *top* label.

```nasm
_start:
    mov esp, stack_top
    push ebx            ; kmain(magic, mbi): cdecl pushes args right to left
    push eax
    call kmain
.hang:
    cli
    hlt
    jmp .hang
```

It pushes `EBX` and `EAX` (cdecl: arguments right-to-left) and calls
`kmain(magic, mbi)`. If `kmain` ever returns, we `cli; hlt` forever.

The flags we request: bit 0 (page-align modules, matters in lesson 12) and bit
1 (give us a memory map, lesson 2).

### `linker.ld` — where things go

The linker script says: "start placing code at address `1M`, in this order:
`.multiboot` header first (it must be near the start of the file), then
`.text`, `.rodata`, `.data`, `.bss`, each page-aligned." It also defines a
symbol `kernel_end` that C can take the address of (lesson 2).

Why 1 MiB? Below it is a minefield of BIOS data, VGA memory and ROMs (see
the memory map in the [README](README.md)).

Try:

```sh
i686-elf-objdump -h build/kernel.elf     # sections and their addresses
i686-elf-nm -n build/kernel.elf          # symbols sorted by address
i686-elf-readelf -l build/kernel.elf     # program headers = what the loader loads
```

### `kmain.c` — crude on purpose

It writes straight to two devices:

- **VGA text buffer at physical `0xB8000`**: an 80×25 array of 16-bit cells.
  Low byte = ASCII char, high byte = color (`0x0F` = white on black). Writing
  to that memory makes characters appear. That's "memory-mapped I/O".
- **Serial port COM1 at I/O port `0x3F8`**: wait until bit 5 of the Line Status
  Register (`0x3F8+5`) says "transmit holding register empty", then `outb` the
  byte. QEMU's `-serial stdio` connects COM1 to your terminal.

Note `volatile` on the VGA pointer: without it the compiler may decide the
writes are useless (nobody reads them) and delete them.

`inb`/`outb` use GCC inline assembly: `"a"(val)` puts `val` in AL/EAX,
`"Nd"(port)` means "an 8-bit constant, or DX". You'll move these into a
header in lesson 1.

---

## 3. Build, run, debug

```sh
make            # build/kernel.elf
make run        # window + serial in terminal. Close the window to quit.
make run-nox    # serial only. Quit with Ctrl-A then X.
make run-test   # no window, no monitor: for scripts, e.g.  timeout 3 make run-test
```

You'll see leftover BIOS/iPXE text around the message on the VGA screen;
lesson 1 adds clearing the screen.

### Debugging with GDB (learn it now, you'll need it in lesson 3)

Terminal 1: `make debug` (QEMU starts *paused*). Terminal 2: `make gdb`. Then:

```
(gdb) break kmain
(gdb) continue
(gdb) next                 # step over a C line
(gdb) info registers
(gdb) x/8xw $esp           # 8 words at the top of the stack
(gdb) layout asm           # TUI disassembly view (Ctrl-X A to leave)
(gdb) stepi                # one instruction
```

### The QEMU logging switch

In the Makefile there's a commented line:

```make
# QEMUFLAGS += -d int,cpu_reset -D $(BUILD)/qemu.log
```

Uncomment it when the machine resets and you don't know why. `build/qemu.log`
will show every exception (`v=0e` = page fault, `v=0d` = general protection,
`v=08` = double fault) with the full register state, including the
`EIP` that faulted. Combined with `i686-elf-addr2line -e build/kernel.elf
<EIP>` this finds most early bugs in a minute.

---

## 4. Exercises (no new modules yet)

1. Change the message color to green on black. Which byte did you change?
2. Remove the `mov esp, stack_top` line, rebuild, run. What happens? (Maybe
   nothing! The loader's ESP happens to be usable. That's exactly why you
   *never* rely on it.) Put it back.
3. Make `kmain` deliberately divide by zero (use a `volatile int` so the
   compiler can't fold it). Run with the QEMU log enabled. Find `v=00` in
   the log, and the triple fault (`v=08`, then `cpu_reset`). Lesson 4 turns this
   into a nice message.
4. Use `i686-elf-objdump -d build/kernel.elf` to find `_start` and `kmain`.
   Check the `push`/`call` sequence matches what you read.

## Pitfalls

- **"`i686-elf-gcc`: command not found"**: Homebrew's bin isn't on your
  `PATH` in that shell. `eval "$(/opt/homebrew/bin/brew shellenv)"`.
- **"no multiboot header found"**: the header isn't in the first 8 KiB, or
  the checksum is wrong, or the section was dropped (check `objdump -h`).
- **QEMU says "Cannot load x86-64 image"**: you built a 64-bit ELF. You must use
  `i686-elf-gcc` and `nasm -f elf32`.

## References

- Multiboot spec, sections 3.1 (header) and 3.2 (machine state): <https://www.gnu.org/software/grub/manual/multiboot/multiboot.html>
- OSDev "Bare Bones": <https://wiki.osdev.org/Bare_Bones>
- OSDev "Serial Ports", "Printing To Screen"
