# Lesson 0 — Environment and Hello World

In this lesson you write the smallest possible kernel: a program that a PC
runs *instead of* an operating system, and that prints one line of text.

```
Hello, world from toyos!
```

That sounds like very little, but getting there means answering questions
that an ordinary C program never has to think about. Who loads your program
into memory, and where? What state is the CPU in when your first instruction
runs? Where is the stack? How do you print anything when there is no
`printf`, no operating system, and no screen driver? By the end of the
lesson you'll know the answer to each of these, and you'll have written the
four files that make up the kernel:

| File | What it is |
|---|---|
| `kernel/arch/i386/boot.asm` | The first instructions that run. Sets up a stack and calls C. |
| `kernel/kmain.c` | The first C function. Writes text to the screen and to the serial port. |
| `kernel/arch/i386/linker.ld` | Tells the linker where in memory each part of the kernel goes. |
| `Makefile` | Builds everything and starts the emulator. |

The root of this repository already contains these files (the "barebone").
To get the most out of this lesson, move them aside and write your own as
you read. The finished versions are in [`code/`](code/) if you get stuck.

**Contents**

1. [What happens when a PC starts](#1-what-happens-when-a-pc-starts)
2. [Installing the tools](#2-installing-the-tools)
3. [The plan: from source files to a running kernel](#3-the-plan-from-source-files-to-a-running-kernel)
4. [The Multiboot header: how the loader finds your kernel](#4-the-multiboot-header-how-the-loader-finds-your-kernel)
5. [The entry point: from the loader to C](#5-the-entry-point-from-the-loader-to-c)
6. [Talking to hardware: the screen and the serial port](#6-talking-to-hardware-the-screen-and-the-serial-port)
7. [The first C function](#7-the-first-c-function)
8. [The linker and the linker script](#8-the-linker-and-the-linker-script)
9. [The Makefile](#9-the-makefile)
10. [Running it](#10-running-it)
11. [Debugging tools you'll need from lesson 3 on](#11-debugging-tools-youll-need-from-lesson-3-on)
12. [Exercises](#12-exercises)

---

## 1. What happens when a PC starts

When you run a program on macOS or Linux, the operating system does a lot
of work for you before `main` runs. It reads the program file from disk,
copies its code and data into memory, sets up a stack, and then jumps to
the first instruction. When the program calls `printf`, the C library
eventually asks the operating system to put the text somewhere.

A kernel *is* the operating system, so none of that exists for it. Instead,
this is what happens when a PC is switched on:

1. The CPU starts running the **BIOS**, a small program stored in a chip on
   the motherboard. The BIOS checks the hardware and finds a disk to boot
   from.
2. The BIOS loads a **bootloader** from that disk. The bootloader is a
   program whose only job is to load the kernel. On Linux machines it's
   usually GRUB.
3. The bootloader reads the kernel file, copies it into memory, prepares
   the CPU, and jumps to the kernel's first instruction.
4. From then on, the kernel is the only software running. It talks to the
   hardware directly.

We won't use a real PC. We use **QEMU**, a program that emulates a whole PC
(CPU, memory, screen, keyboard, disks) on your Mac. QEMU has its own BIOS,
and it has a built-in bootloader too: if you start it with
`-kernel kernel.elf`, it loads `kernel.elf` itself and jumps into it. That
saves us from writing a bootloader, which is a whole project of its own.

QEMU's built-in loader follows a published standard called **Multiboot**.
Multiboot is an agreement between bootloaders and kernels: the kernel puts
a small marker in its file to say "I'm a Multiboot kernel", and in return
the bootloader promises to load the kernel in a well-defined way and leave
the CPU in a well-defined state. GRUB follows the same standard, so the
kernel you write would boot on a real PC with GRUB too. Section 4 covers
this agreement in detail.

---

## 2. Installing the tools

### Why you can't use the compiler on your Mac

A compiler doesn't just turn C into machine code. It produces machine code
for **one particular CPU**, packaged in **one particular file format**, and
it assumes **one particular operating system** underneath. The `clang` that
comes with macOS on an Apple Silicon Mac produces:

- ARM64 machine code (the instructions of Apple's CPUs),
- in the Mach-O file format (the format macOS uses for programs),
- assuming macOS is there (for example, it links your program against
  macOS's C library).

We need something different on all three counts:

- **32-bit x86** machine code, because the emulated PC has a 32-bit Intel
  CPU (an "i386", or "i686" for the slightly newer models).
- The **ELF** file format, because that's what the Multiboot loader reads.
  ELF is the standard format for programs on Linux and most Unix systems.
- **No operating system** underneath, because we're writing it.

A compiler that runs on one machine but produces code for a different one
is called a **cross-compiler**. The one we use is called `i686-elf-gcc`:
it's GCC, producing i686 code, in ELF files, with no operating system
assumed. Its name follows the convention *CPU-format-compiler*.

### The tools

| Tool | What it does for us |
|---|---|
| `i686-elf-gcc` | The cross-compiler for C. Also comes with the cross linker `i686-elf-ld` and inspection tools (`i686-elf-objdump`, `i686-elf-nm`, `i686-elf-readelf`, `i686-elf-addr2line`) that you'll use to look inside the files you build. |
| `nasm` | An assembler. A few parts of a kernel can't be written in C (the very first instructions, for example), so we write them in assembly. NASM uses Intel syntax, the one you know. |
| `qemu` | The emulator. We use the program `qemu-system-i386` from it, which emulates a 32-bit PC. |
| `i386-elf-gdb` | A debugger that understands 32-bit x86 programs. It can connect to QEMU and debug your kernel while it runs. |
| `e2fsprogs` | Tools to create and check disk images. You won't need them until lesson 16. |

Install them all with Homebrew:

```sh
brew install i686-elf-gcc nasm qemu i386-elf-gdb e2fsprogs
```

Check that they work:

```
$ i686-elf-gcc --version | head -1
i686-elf-gcc (GCC) 16.2.0
$ nasm -v
NASM version 3.02 compiled on Jun 29 2026
$ qemu-system-i386 --version | head -1
QEMU emulator version 11.1.1
```

Your version numbers may differ, and that's fine. If the shell says
`command not found`, Homebrew's folder isn't on your `PATH` in that
terminal. Run `eval "$(/opt/homebrew/bin/brew shellenv)"` and try again.

---

## 3. The plan: from source files to a running kernel

Here is how the four files turn into something that runs:

```
 boot.asm ──► nasm ──────────► boot.asm.o ─┐
                                           ├──► linker ──► kernel.elf ──► QEMU
 kmain.c  ──► i686-elf-gcc ──► kmain.o ────┘      ▲
                                                  │
                                             linker.ld
```

Each source file is first turned into an **object file** (`.o`). An object
file contains machine code, but it isn't a finished program yet: it doesn't
know at which memory address its code will end up, and it may refer to
functions that live in other object files. The **linker** combines all the
object files into one file, `kernel.elf`, and decides where everything
goes in memory. The **linker script** `linker.ld` is how we tell the linker
what we want. We'll write the files in this order:

1. `boot.asm`, because it contains both the Multiboot marker and the first
   instructions that run (sections 4 and 5).
2. `kmain.c`, the first C code, which prints our message (sections 6 and 7).
3. `linker.ld`, once we have two object files and can see what the linker
   has to do with them (section 8).
4. The `Makefile`, which runs all the commands (section 9).

---

## 4. The Multiboot header: how the loader finds your kernel

### The problem

QEMU's loader is given a file and has to decide whether it knows how to
boot it. It needs some sign that the file is a kernel that follows the
Multiboot rules, as opposed to a random file or a kernel built for some
other loader. And it needs to know what the kernel wants from it.

### The solution: a header with a magic number

The Multiboot standard says that a kernel must contain a **Multiboot
header** somewhere in the **first 8 KiB of the file**, aligned on a 4-byte
boundary. The loader scans that part of the file looking for it. The
header is just three 32-bit numbers in a row:

| Offset | Field | Value | Meaning |
|---|---|---|---|
| 0 | `magic` | `0x1BADB002` | A fixed number that identifies the header. The loader looks for exactly this value. |
| 4 | `flags` | `0x00000003` | Which optional services we want from the loader (explained below). |
| 8 | `checksum` | `0xE4524FFB` | Chosen so that `magic + flags + checksum` is 0. |

A "magic number" is simply a value that is unlikely to appear by accident.
`0x1BADB002` was chosen because it reads a bit like "1 bad boot" in hex.

The **flags** field is a set of bits, each requesting something from the
loader. We set two of them:

- **Bit 0** asks the loader to place any extra files it loads alongside the
  kernel (called "modules") on 4 KiB boundaries. We don't load any modules,
  but it's customary to set this bit and costs nothing.
- **Bit 1** asks the loader to tell us how much memory the machine has and
  which parts of it are usable. We'll use that information in lesson 2.

So `flags = (1 << 0) | (1 << 1) = 0x3`.

The **checksum** protects against a false match. If some random data in
your file happened to contain `0x1BADB002`, the two words after it would
almost certainly not satisfy the rule. The rule is that the three numbers
must add up to zero, using 32-bit arithmetic where anything that overflows
past 32 bits is thrown away. So the checksum is the negative of the other
two:

```
magic + flags            = 0x1BADB002 + 0x00000003 = 0x1BADB005
checksum = -(0x1BADB005) = 0xE4524FFB          (two's complement, 32 bits)

check:  0x1BADB005 + 0xE4524FFB = 0x1_00000000
        keep only the low 32 bits:  0x00000000   ✓
```

### Writing it in NASM

Create `kernel/arch/i386/boot.asm` and start it with the header. First,
give the numbers names with `equ`. An `equ` defines a constant for the
assembler, like `#define` in C. It doesn't put anything in the output file:

```nasm
MB_MAGIC    equ 0x1BADB002
MB_ALIGN    equ 1 << 0              ; page-align boot modules
MB_MEMINFO  equ 1 << 1              ; ask for a memory map
MB_FLAGS    equ MB_ALIGN | MB_MEMINFO
MB_CHECKSUM equ -(MB_MAGIC + MB_FLAGS)
```

NASM computes `-(MB_MAGIC + MB_FLAGS)` for you, so you never have to work
out `0xE4524FFB` by hand.

Next, put the three numbers into the file:

```nasm
section .multiboot
align 4
    dd MB_MAGIC
    dd MB_FLAGS
    dd MB_CHECKSUM
```

`dd` ("define doubleword") emits a 32-bit value into the output. `align 4`
makes sure the header starts at an address that is a multiple of 4, as the
standard requires.

The line `section .multiboot` needs some explanation, because sections
matter a lot in this lesson. An object file is divided into **sections**,
which are named chunks of bytes. By convention, machine code goes in a
section called `.text`, and global variables go in `.data` or `.bss`. You
can also invent your own section names, which is what we do here: we put
the header in a section of its own called `.multiboot`. Why? Because the
header has to end up near the start of the final file, and the linker
places sections wherever we tell it to. Keeping the header in its own
section lets us tell the linker "put this one first". You'll see exactly
how in section 8.

---

## 5. The entry point: from the loader to C

### What the loader leaves us with

After the loader has copied the kernel into memory, it jumps to the
kernel's **entry point**, the address of its first instruction. The
Multiboot standard (section 3.2, "Machine state") describes exactly what
the CPU looks like at that moment. These are the parts that matter to us:

- **The CPU is in 32-bit protected mode.** An x86 CPU starts up in a
  backward-compatible 16-bit mode (called "real mode") that can only use
  1 MiB of memory. The loader has already switched it to 32-bit mode, where
  registers and addresses are 32 bits wide and the CPU can reach 4 GiB.
  That's the mode C compilers produce code for.
- **Paging is off.** Paging is the CPU feature behind virtual memory
  (lessons 7 and 8). With paging off, an address in your program is the
  real, physical address in RAM: address `0x100000` is the byte 1 MiB from
  the start of memory.
- **Interrupts are disabled.** Interrupts are how hardware devices get the
  CPU's attention (lesson 5). Until we've prepared for them, it's important
  that they stay off.
- **Register `EAX` contains `0x2BADB002`.** This is how the kernel can tell
  that it was started by a Multiboot loader. Note that this is a different
  number from the one in the header.
- **Register `EBX` contains the address of the Multiboot information
  structure**, a block of memory where the loader wrote down what it knows
  about the machine (the amount of memory, for example). We'll read it in
  lesson 2.
- **Register `ESP`, the stack pointer, has no defined value.** The
  standard says the kernel must set up its own stack before using one.

The segment registers (`CS`, `DS`, `SS`...) are also set up by the loader
in a way that works. What segments are, and why we'll replace the loader's
setup with our own, is the subject of lesson 3. For now, all you need to
know is that they let us use any address from 0 to 4 GiB directly.

Here is the real state of the CPU when our kernel's first instruction is
about to run, captured with the debugger (section 11 shows how):

```
eax            0x2badb002          the Multiboot "booted by a loader" value
ebx            0x9500              the Multiboot information is at address 0x9500
esp            0x6f10              left over from the loader: not ours
eip            0x1000e0            our first instruction, _start
eflags         0x6                 bit 9 (IF, "interrupts enabled") is 0
```

### Why we need our own stack

C code uses the stack all the time. A `call` instruction pushes the return
address on it. A function's local variables usually live on it. Arguments
are passed on it. So before we call any C function, `ESP` must point to a
piece of memory that we own and that nothing else will overwrite.

`ESP` currently holds `0x6f10`, a value the loader happened to leave behind.
That address is in a region of low memory that the BIOS and the loader
used for their own purposes. It might work, but we have no guarantee about
what else lives there, so we reserve memory for a stack ourselves.

### Reserving memory for the stack

We'll reserve 16 KiB, which is plenty for now. We don't want those 16 KiB
to take up space in the kernel file, because their initial contents don't
matter. That's what the **`.bss` section** is for. `.bss` holds data that
starts out as zero; the file only records *how big* it is, and the loader
reserves that much memory and fills it with zeros. (You've used `.bss`
before without knowing: in C, a global variable without an initializer,
like `int counter;`, goes in `.bss`.)

```nasm
section .bss
align 16
stack_bottom:
    resb 16384                      ; 16 KiB boot stack
stack_top:
```

`resb 16384` means "reserve 16384 bytes". The two labels mark the two ends
of that area. On x86 the stack **grows downwards**: `push` first subtracts
4 from `ESP`, then stores the value at the new `ESP`. So an empty stack has
`ESP` pointing at the **top** end, and that's the label we load into `ESP`:

```
higher addresses
   stack_top    ──►  ┌──────────────┐  ◄── ESP starts here (empty stack)
                     │              │
                     │   16 KiB     │      each push moves ESP down
                     │              │
   stack_bottom ──►  └──────────────┘  ◄── if ESP ever gets here, we've overflowed
lower addresses
```

`align 16` places the stack on a 16-byte boundary. The CPU doesn't require
it, but GCC assumes the stack is 16-byte aligned at function calls when it
uses certain instructions, so it's a good habit.

### The first instructions

Now the code itself, in the `.text` section:

```nasm
section .text
global _start
extern kmain

_start:
    mov esp, stack_top

    ; kmain(uint32_t magic, uint32_t mbi_phys) -- cdecl: push args right to left
    push ebx
    push eax
    call kmain

    ; kmain should never return; if it does, halt forever.
.hang:
    cli
    hlt
    jmp .hang
```

Line by line:

- `global _start` makes the label `_start` visible outside this file. The
  linker needs to see it, because `_start` will be the kernel's entry point
  (we tell the linker so in section 8).
- `extern kmain` tells NASM that `kmain` is defined in another file. NASM
  will leave a note for the linker saying "fill in the address of `kmain`
  here" (section 8 shows these notes).
- `mov esp, stack_top` sets up our stack. This must be the very first
  thing we do.
- `push ebx`, `push eax`, `call kmain` call the C function
  `kmain(magic, mbi)` and pass it the two values the loader gave us.

Why push `EBX` first? GCC uses the standard 32-bit x86 **calling
convention**, called **cdecl**. It says that arguments are passed on the
stack, pushed from **right to left**, so that the first argument ends up
closest to the top of the stack. `kmain`'s first parameter is `magic`
(in `EAX`) and its second is `mbi` (in `EBX`), so we push `EBX` first, then
`EAX`. Then `call` pushes the return address. Here's the stack at the
moment `kmain` starts, with real addresses from the debugger:

```
address      value          what it is
0x106000                    stack_top (ESP pointed here after "mov esp")
0x105ffc     0x00009500     pushed first:  EBX  = 2nd argument, mbi
0x105ff8     0x2badb002     pushed second: EAX  = 1st argument, magic
0x105ff4     0x001000ec     pushed by call: return address (.hang)  ◄── ESP
```

From `kmain`'s point of view, its first argument is at `ESP+4` and its
second at `ESP+8`, which is exactly where cdecl says they should be. The
return address `0x1000ec` is the address of `.hang`, the instruction right
after the `call`.

Finally, what if `kmain` returns? There's nothing to return *to*: no
operating system will clean up after us. So we stop the CPU:

- `cli` ("clear interrupt flag") disables interrupts.
- `hlt` ("halt") stops the CPU until an interrupt arrives. With interrupts
  disabled, that should be forever.
- `jmp .hang` is there just in case. A few special events, such as a
  "non-maskable interrupt" from failing hardware, can wake the CPU even
  with interrupts disabled. If that ever happens, we go back to halting.

(A label starting with a dot, like `.hang`, is a "local label" in NASM: its
full name is `_start.hang`, so it won't clash with a `.hang` elsewhere.)

<details>
<summary>The complete <code>boot.asm</code></summary>

```nasm
; boot.asm -- Multiboot header and kernel entry point.
;
; A Multiboot loader (QEMU's -kernel, or GRUB) finds the header below,
; loads our ELF at the addresses in linker.ld, and jumps to _start with:
;   - the CPU in 32-bit protected mode, paging OFF, interrupts OFF
;   - EAX = 0x2BADB002 (proof we were booted by a Multiboot loader)
;   - EBX = physical address of the Multiboot information structure
;   - ESP = undefined  (we must set up our own stack before calling C)

MB_MAGIC    equ 0x1BADB002
MB_ALIGN    equ 1 << 0              ; page-align boot modules
MB_MEMINFO  equ 1 << 1              ; ask for a memory map
MB_FLAGS    equ MB_ALIGN | MB_MEMINFO
MB_CHECKSUM equ -(MB_MAGIC + MB_FLAGS)

section .multiboot
align 4
    dd MB_MAGIC
    dd MB_FLAGS
    dd MB_CHECKSUM

section .bss
align 16
stack_bottom:
    resb 16384                      ; 16 KiB boot stack
stack_top:

section .text
global _start
extern kmain

_start:
    mov esp, stack_top

    ; kmain(uint32_t magic, uint32_t mbi_phys) -- cdecl: push args right to left
    push ebx
    push eax
    call kmain

    ; kmain should never return; if it does, halt forever.
.hang:
    cli
    hlt
    jmp .hang
```

</details>

---

## 6. Talking to hardware: the screen and the serial port

`kmain` has to print a message, and there is nothing to help it: no
`printf`, no operating system, no drivers. To print, the kernel has to talk
to the hardware directly. This section explains the two devices we use and
how the CPU talks to them. The next section turns that into code.

### Two ways for the CPU to talk to a device

On a PC, the CPU communicates with devices in one of two ways.

**Memory-mapped I/O.** Some devices are wired so that a range of memory
addresses belongs to them instead of to RAM. When the CPU writes to one of
those addresses, the value goes to the device, not to a memory chip. In C,
that's just a pointer write: you don't need any special instruction.

**I/O ports.** The x86 CPU also has a second, completely separate set of
addresses, used only for talking to devices. They're called **I/O ports**
and are numbered from 0 to 65535 (`0xFFFF`). You can't reach them with a
normal `mov`: they have their own two instructions, `out` (send a value to
a port) and `in` (read a value from a port). Each device listens on a few
port numbers, which were fixed by the original IBM PC design in the 1980s
and have stayed the same ever since.

We'll use one device of each kind.

### The screen: VGA text mode

When the PC starts, the graphics card is in **text mode**: the screen shows
25 rows of 80 characters each, and the graphics card draws the letters for
us. (This is the mode the original IBM PC's "VGA" graphics card used, and
every PC graphics card, including QEMU's emulated one, still supports it.)

What the screen shows is stored in memory starting at the physical address
**`0xB8000`**. This is memory-mapped I/O: the graphics card reads that
memory many times per second and draws whatever it finds. To put a letter
on the screen, you write it to the right address.

Each of the 80 × 25 = 2000 positions on the screen (called a **cell**) is
described by **2 bytes**:

- the first byte is the **character**, as an ASCII code;
- the second byte is the **color**: the low 4 bits are the color of the
  letter, the high 4 bits the color of the background.

So the whole screen takes 2000 × 2 = 4000 bytes, from `0xB8000` to
`0xB8F9F`. The cells are stored row by row: the first 80 cells are the top
row, the next 80 the second row, and so on. The cell at row `r` and column
`c` (both counting from 0) is cell number `r * 80 + c`, and since each cell
is 2 bytes, its address is:

```
address = 0xB8000 + 2 * (r * 80 + c)
```

For example, row 3, column 10 is cell `3 * 80 + 10 = 250`, at address
`0xB8000 + 500 = 0xB81F4`.

Colors are numbered 0 to 15 (0 is black, 7 light grey, 15 white, 2 green,
4 red...). White letters on a black background is foreground 15 (`0xF`)
and background 0, so the color byte is `0x0F`.

Because x86 is a **little-endian** CPU (the low byte of a number is stored
at the lower address), it's convenient to write a whole cell as one 16-bit
value, with the color in the high byte and the character in the low byte:

```
cell value = (color << 8) | character

'H' in white on black:  (0x0F << 8) | 0x48  =  0x0F48
in memory:   0xB8000: 48    0xB8001: 0F
                      'H'            color
```

Here's what the first 8 cells look like after our kernel has run, read
from memory with the debugger. You can see `H`, `e`, `l`, `l`, `o`, `,`,
space, `w`, each with color `0x0F`:

```
(gdb) x/8xh 0xb8000
0xb8000:  0x0f48  0x0f65  0x0f6c  0x0f6c  0x0f6f  0x0f2c  0x0f20  0x0f77
```

In C, we'll access the screen through a pointer to 16-bit values:

```c
#define VGA_TEXT ((volatile uint16_t *)0xB8000)

VGA_TEXT[0] = (0x0F << 8) | 'H';   /* top-left cell */
```

The word **`volatile`** is essential here. It tells the compiler "every
read and write through this pointer matters, even if it looks useless to
you". Without it, GCC sees that we write to memory that the program never
reads back, decides the writes have no effect, and is allowed to delete
them. The compiler doesn't know that a graphics card is reading that
memory; `volatile` is how we tell it.

### The serial port: COM1

The screen is nice to look at, but it's not very practical for debugging.
It only holds 25 lines, older lines are lost forever when it scrolls, you
can't copy text out of it, and everything on it disappears when QEMU
closes. We want a second output that lands in a normal terminal window on
the Mac.

That's what the **serial port** gives us. A serial port is an old kind of
connector that PCs used for decades to talk to modems, mice, and other
computers. It sends data one bit at a time over a wire, which is where the
name "serial" comes from. Inside the PC, the serial port is driven by a
chip called a **UART** ("universal asynchronous receiver-transmitter"). The
model that all PCs emulate is the **16550 UART**. Your program gives the
UART one byte; the UART sends it down the wire bit by bit.

PCs came with up to four serial ports, named **COM1** to **COM4**. Each
UART is controlled through **8 consecutive I/O ports**. For COM1, they
start at port **`0x3F8`**, so COM1 uses ports `0x3F8` to `0x3FF`. Each of
these ports gives access to one of the UART's internal **registers**, small
storage locations inside the chip that control it or report its state.

Here's why it's useful to us: QEMU can connect the emulated COM1 to your
terminal. With the option **`-serial stdio`**, every byte the kernel sends
to COM1 is printed in the terminal where you started QEMU. Every key you
type in that terminal goes the other way, into COM1 (we'll read it in
lesson 18). So the serial port becomes a simple text channel between the
kernel and your Mac, with scrollback, copy and paste, and the ability to
save it to a file. Many times in later lessons, the screen will be useless
because the kernel crashed in the middle of something, and the text on the
serial port is what will tell you what happened.

To send one byte, we need two of COM1's registers:

| Port | Register | What we do with it |
|---|---|---|
| `0x3F8` (COM1 + 0) | **Data** | Writing a byte here hands it to the UART to send. |
| `0x3FD` (COM1 + 5) | **Line Status** | Reading it tells us the UART's state. **Bit 5** is 1 when the UART is ready to accept the next byte. |

Why do we need to check whether the UART is ready? Because sending is
slow. A serial line typically runs at a few thousand bytes per second,
while the CPU can execute millions of instructions in that time. The UART
holds one byte while it sends it out bit by bit. If we write a new byte
before the previous one has left, the new one replaces it and the old one
is lost. So the rule is: **wait until bit 5 of the Line Status register is
1, then write the byte to the Data register**. (QEMU's emulated UART is
ready almost immediately, but real hardware isn't, and writing the wait
loop now means the code is correct everywhere.)

Real serial hardware needs some configuration before use (the speed, the
format of each byte). QEMU's emulated UART works without it, so we skip it
in this lesson. Lesson 1 adds the proper setup.

One last detail: **line endings**. In C, a line ends with `'\n'` (line
feed, byte `0x0A`), which on a terminal means "move down one line". To
also go back to the left edge, a terminal expects `'\r'` (carriage return,
byte `0x0D`) as well. These names come from typewriters, where moving the
paper up and returning the carriage to the left were two separate actions.
So when we send `'\n'` to the serial port, we'll send `'\r'` before it.

### `in` and `out` in C: inline assembly

C has no way to say "execute the `out` instruction". GCC lets you put
assembly instructions inside C code, a feature called **inline assembly**.
We'll write two tiny functions, `outb` ("output a byte") and `inb` ("input
a byte"), that each contain one instruction:

```c
static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}
```

The syntax looks strange, so here it is piece by piece. The general form
is:

```
__asm__ volatile ( "instruction template" : outputs : inputs );
```

- **The instruction template** is written in **AT&T syntax**, GCC's default,
  not the Intel syntax you know. The main difference is that the operands
  are reversed: AT&T `outb %0, %1` means "out *to* %1, *from* %0". In Intel
  syntax this is `out dx, al`.
- **`%0`, `%1`** are placeholders for the operands listed after the colons,
  numbered in order: outputs first, then inputs.
- **The outputs and inputs** tell GCC where each C value must be.
  `"a"(val)` means "put `val` in the `A` register" (`AL` for a byte,
  because `val` is a `uint8_t`). The `out` instruction requires its data
  to be in `AL`, so we must say so. `"Nd"(port)` means "the port number is
  either a constant from 0 to 255 (`N`) or in the `DX` register (`d`)",
  which are the two forms `out` accepts. `"=a"(val)` in `inb` means "after
  the instruction, `val` is in `AL`"; the `=` marks it as an output.
- **`volatile`** after `__asm__` tells GCC not to remove or move the
  instruction, even if it can't see its result being used. Talking to
  hardware is a side effect GCC doesn't understand.
- **`static inline`** asks GCC to paste the function's body directly where
  it's called, so `outb(0x3F8, 'H')` becomes a single `out` instruction,
  with no function call.

You don't need to memorize this. You'll write these two functions once and
use them for the rest of the project.

---

## 7. The first C function

Now we can write `kernel/kmain.c`. It does four things:

1. It checks that `magic` is `0x2BADB002`, to be sure we were started by a
   Multiboot loader.
2. It has a function `serial_putc(c)` that sends one character to COM1:
   wait for bit 5 of the Line Status register (port `0x3F8 + 5`), then
   `outb` the character to port `0x3F8`.
3. It has a function `say(s)` that goes through a string and, for each
   character, writes it to VGA cell number `i` (the `i`-th character goes
   in cell `i`, so the message appears on the top row) and sends it to
   the serial port. For `'\n'`, it sends `'\r'` to the serial port first,
   and doesn't write anything on the screen.
4. After printing, it halts the CPU in a loop, like `_start` does.

Note that `say` is crude. It always writes from the top-left corner of the
screen, so a second call would overwrite the first, and a `'\n'` doesn't
move to the next line on screen. That's fine for one message. Lesson 1
writes a proper screen driver.

Two smaller C details:

- Include `<stdint.h>` for `uint8_t`, `uint16_t` and `uint32_t`. This works
  even with no C library, because `<stdint.h>` is one of the few headers
  the compiler itself provides (section 9 explains why).
- `kmain` doesn't use its `mbi` argument yet. Write `(void)mbi;` to tell
  the compiler that's deliberate, or it'll warn about an unused parameter.

Try writing it yourself before opening the reference below.

<details>
<summary>The complete <code>kmain.c</code></summary>

```c
/* kmain.c -- first C code that runs.
 *
 * Everything here is deliberately crude: VGA and serial are poked directly.
 * Lesson 1 replaces this with proper drivers (dev/vga.c, dev/serial.c) and
 * a kprintf().
 */
#include <stdint.h>

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002

#define VGA_TEXT   ((volatile uint16_t *)0xB8000)
#define VGA_COLOR  0x0F                 /* white on black */

#define COM1 0x3F8

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static void serial_putc(char c)
{
    while ((inb(COM1 + 5) & 0x20) == 0)     /* wait for "transmit empty" */
        ;
    outb(COM1, (uint8_t)c);
}

static void say(const char *s)
{
    for (int i = 0; s[i]; i++) {
        if (s[i] == '\n') {
            serial_putc('\r');          /* terminals want CR LF */
        } else {
            VGA_TEXT[i] = (uint16_t)(VGA_COLOR << 8) | (uint8_t)s[i];
        }
        serial_putc(s[i]);
    }
}

void kmain(uint32_t magic, uint32_t mbi_phys)
{
    (void)mbi_phys;

    if (magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        say("Not booted by a Multiboot loader!\n");
        return;
    }

    say("Hello, world from toyos!\n");

    for (;;)
        __asm__ volatile ("hlt");
}
```

`0x20` is bit 5 (`1 << 5`). Notice that when the magic number is wrong,
`kmain` returns, and `_start` then halts the CPU.

</details>

---

## 8. The linker and the linker script

We now have two source files. Each one can be compiled or assembled into an
object file on its own. This section explains what's inside those object
files, what the linker does to combine them, and why we have to give the
linker instructions in a **linker script**.

### What's inside an object file

Let's look inside `boot.asm.o`, the object file NASM produces from
`boot.asm`. The tool `i686-elf-objdump -h` lists the sections in a file
(here trimmed to the ones we wrote):

```
$ i686-elf-objdump -h build/kernel/arch/i386/boot.asm.o
Idx Name          Size      VMA       LMA       File off  Algn
  0 .multiboot    0000000c  00000000  00000000  00000310  2**2
  1 .bss          00004000  00000000  00000000  00000320  2**4
  2 .text         00000010  00000000  00000000  00000320  2**4
```

These are the three sections we created in `boot.asm`. The `Size` column
makes sense: the header is `0xc` = 12 bytes (three 4-byte numbers), the
stack is `0x4000` = 16384 bytes, and our code is `0x10` = 16 bytes. The
`VMA` column is the memory address of each section, and it's **0 for all
of them**. NASM doesn't know where in memory any of this will end up, so it
pretends each section starts at 0.

`kmain.o`, produced by GCC from `kmain.c`, looks similar: a `.text`
section with `kmain`'s code, and sections whose names start with `.rodata`
holding the string constants (`"Hello, world from toyos!\n"`). All at
address 0 too.

That raises a problem. Look at the machine code for `_start` in the object
file:

```
$ i686-elf-objdump -d -M intel build/kernel/arch/i386/boot.asm.o
00000000 <_start>:
   0:   bc 00 40 00 00          mov    esp,0x4000
   5:   53                      push   ebx
   6:   50                      push   eax
   7:   e8 fc ff ff ff          call   8 <_start+0x8>
```

`mov esp, stack_top` has become `mov esp, 0x4000`. That's where `stack_top`
is if `.bss` starts at address 0 (it's 0x4000 bytes after `stack_bottom`).
But `.bss` won't really be at address 0, so this value is wrong. And the
`call kmain` contains a meaningless placeholder value (`fc ff ff ff`),
because `kmain` isn't even in this file.

The assembler knows these values are incomplete, and it leaves notes in the
object file saying which bytes need fixing later. These notes are called
**relocations**. You can list them with `objdump -r`:

```
$ i686-elf-objdump -r build/kernel/arch/i386/boot.asm.o
RELOCATION RECORDS FOR [.text]:
OFFSET   TYPE              VALUE
00000001 R_386_32          .bss
00000008 R_386_PC32        kmain
```

The first note says: "the 4 bytes at offset 1 in `.text` (the operand of
the `mov`) must be increased by the final address of `.bss`". The second
says: "the 4 bytes at offset 8 (the operand of the `call`) must become the
distance to `kmain`, wherever it ends up". (`call` on x86 stores the
distance from the next instruction to the target, not the target's address
itself. `PC32` means "relative to the program counter".)

### What the linker does

The **linker** takes all the object files and does three things:

1. **It merges sections.** All the `.text` sections from all the object
   files are put one after another into a single `.text` section in the
   output file. Same for `.rodata`, `.data` and `.bss`. The sections in
   the final file are called **output sections**; the ones it reads from
   the object files are **input sections**.
2. **It assigns addresses.** Each output section gets a real memory
   address. Now every label has a final address: `_start`, `kmain`,
   `stack_top`...
3. **It applies the relocations.** For every note, it computes the right
   value and patches the bytes.

It then writes the result as an executable ELF file. Besides the sections,
an executable ELF contains a short list of **program headers** (also called
**segments**) that say "copy these bytes from the file to this memory
address". That list is what the Multiboot loader reads to put the kernel in
memory. The ELF file also records the **entry point**, the address the
loader jumps to.

### What goes wrong without a linker script

If you don't give the linker any instructions, it uses its built-in
default, which is designed for normal programs running on a Unix system.
Let's try it:

```
$ i686-elf-gcc -nostdlib -ffreestanding -o build/nols.elf \
      build/kernel/kmain.o build/kernel/arch/i386/boot.asm.o -lgcc
$ i686-elf-readelf -l build/nols.elf
Entry point 0x8048150
  LOAD           0x000000 0x08048000 0x08048000 0x001ac 0x001ac R E 0x1000
  ...
```

The default places the program at address **`0x08048000`**, which is
about 128.3 MiB. That's the traditional start address for programs on
32-bit Linux, where each program has its own virtual address space and
such a high address is fine. For us, with paging off, it means "load the
kernel into physical RAM at 128.3 MiB". Our QEMU machine has exactly 128
MiB of RAM, so that address **doesn't exist**. If you boot this file with
`-m 128M`, nothing at all is printed. Give QEMU 256 MiB of memory instead
and the same file works. The kernel isn't wrong; it was just placed at an
address where there's no memory.

We need to choose the address ourselves. That's what the linker script is
for.

### Where should the kernel go?

With paging off, the address we choose is a real location in RAM. On a PC,
the first megabyte of memory is crowded with things left over from the
original IBM PC:

```
0x00000000 ┌─────────────────────────────────────┐
           │ interrupt table and BIOS data       │  used by the BIOS
0x00000500 ├─────────────────────────────────────┤
           │ free memory (but the BIOS and the   │  the loader put its info at
           │ loader use parts of it at boot)     │  0x9500 and its stack at 0x6f10
0x0009FC00 ├─────────────────────────────────────┤
           │ extended BIOS data area             │  used by the BIOS
0x000A0000 ├─────────────────────────────────────┤
           │ video memory (VGA text at 0xB8000)  │  not RAM: the graphics card
0x000C0000 ├─────────────────────────────────────┤
           │ BIOS ROMs                           │  not RAM: read-only chips
0x00100000 ├─────────────────────────────────────┤  ◄── 1 MiB
           │ free RAM, up to the end of memory   │  our kernel goes here
           │                                     │
0x08000000 └─────────────────────────────────────┘  end of RAM with -m 128M
```

Everything above **1 MiB** (`0x100000`) is plain, unused RAM. That's where
kernels traditionally put themselves, and it's what we'll do.

### Writing the linker script, line by line

A linker script is a text file in the linker's own small language. Create
`kernel/arch/i386/linker.ld`. We'll write it in four steps.

**Step 1: the entry point.**

```
ENTRY(_start)
```

This writes the address of `_start` into the ELF file as the entry point.
The loader reads it and jumps there.

**Step 2: the start address.**

```
SECTIONS
{
    . = 1M;
}
```

Everything about placing sections goes inside `SECTIONS { ... }`. The
special symbol `.` (a single dot) is called the **location counter**. It's
the address where the linker will place the next thing. It starts at 0, and
`. = 1M;` sets it to 1 MiB (`0x100000`). `1M` is linker-script shorthand
for 1024 × 1024; you can also write `0x100000`.

**Step 3: the output sections.**

Inside `SECTIONS`, after setting the location counter, we list the output
sections in the order we want them in memory. Each one is written as:

```
    name : { which input sections go into it }
```

Inside the braces, a pattern like `*(.text)` means "the `.text` sections of
all input files". The `*` matches any file name. You can list several
section names inside the parentheses, and use `*` in section names too:
`*(.rodata .rodata.*)` means "every section called `.rodata`, or whose name
starts with `.rodata.`". We need that pattern because GCC splits its data
into sections with longer names, like the `.rodata.str1.1` it uses for
string constants in `kmain.o`.

Here are our four output sections:

```
SECTIONS
{
    . = 1M;

    .text :
    {
        *(.multiboot)
        *(.text .text.*)
    }

    .rodata :
    {
        *(.rodata .rodata.*)
    }

    .data :
    {
        *(.data .data.*)
    }

    .bss :
    {
        *(COMMON)
        *(.bss .bss.*)
    }
}
```

The linker places `.text` at the current location counter, 1 MiB, then
moves the counter forward by the size of `.text`, then places `.rodata`
there, and so on.

Notice the first line in `.text`: **`*(.multiboot)`**. This is why we put
the Multiboot header in its own section. Because it's listed first, the
header becomes the very first bytes of `.text`, which is the first section
in the file. That guarantees it's within the first 8 KiB of the file,
where the loader looks for it.

`*(COMMON)` in `.bss` is for a historical quirk of C compilers: in some
modes, uninitialized global variables are put in a pseudo-section called
`COMMON` instead of `.bss`. Listing it means they also end up in `.bss`.

**Step 4: page alignment and an end marker.**

Two final additions:

```
    .text ALIGN(4K) :
    ...
    .rodata ALIGN(4K) :
    ...

    kernel_end = .;
```

`ALIGN(4K)` after a section name rounds the location counter up to the next
multiple of 4096 before placing the section. So each section starts on its
own 4 KiB boundary. The reason is paging, which arrives in lesson 7: the
CPU's memory protection works in blocks of 4 KiB called **pages**, and
each page can be made read-only or writable, but not half of each. Keeping
code (`.text`) and data (`.data`) on separate pages lets us protect them
differently later. It wastes a few kilobytes, which doesn't matter.

`kernel_end = .;` at the very end defines a new symbol, `kernel_end`, whose
value is the location counter after the last section: the first address
after the whole kernel. A symbol defined in a linker script has an address
but no storage. In C you can declare it with `extern char kernel_end[];`
and then use `kernel_end` as an address. Lesson 2 uses it to know where the
kernel ends and free memory begins.

<details>
<summary>The complete <code>linker.ld</code></summary>

```
/* linker.ld -- where each part of the kernel lives in memory.
 *
 * For now the kernel is linked AND loaded at physical 1 MiB, with paging
 * off, so virtual address == physical address. Lesson 8 changes this to a
 * "higher-half" kernel linked at 0xC0100000 but still loaded at 1 MiB.
 */
ENTRY(_start)

SECTIONS
{
    . = 1M;

    /* The Multiboot header must be in the first 8 KiB of the file. */
    .text ALIGN(4K) :
    {
        *(.multiboot)
        *(.text .text.*)
    }

    .rodata ALIGN(4K) :
    {
        *(.rodata .rodata.*)
    }

    .data ALIGN(4K) :
    {
        *(.data .data.*)
    }

    .bss ALIGN(4K) :
    {
        *(COMMON)
        *(.bss .bss.*)
    }

    kernel_end = .;
}
```

</details>

### Checking the result

Once the Makefile from the next section is written and `make` has run, you
can check that the linker did what we asked. These are the real results
for the reference code.

The sections, with their final addresses (`VMA`) and their positions in the
file (`File off`):

```
$ i686-elf-objdump -h build/kernel.elf
Idx Name          Size      VMA       LMA       File off  Algn
  0 .text         000000f0  00100000  00100000  00001000  2**4
  1 .rodata       0000003d  00101000  00101000  00002000  2**2
  2 .bss          00004000  00102000  00102000  00003000  2**4
  ...
```

`.text` starts at 1 MiB, and each section starts on a 4 KiB boundary.
There's no `.data` section because we have no initialized global variables
yet; the linker drops empty sections.

The Multiboot header, at the start of `.text`, is at offset `0x1000` in the
file, well within the first 8 KiB. `xxd` shows the raw bytes there:

```
$ xxd -s 0x1000 -l 12 build/kernel.elf
00001000: 02b0 ad1b 0300 0000 fb4f 52e4            .........OR.
```

Read them 4 bytes at a time and remember that x86 is little-endian (lowest
byte first): `02 b0 ad 1b` is `0x1BADB002`, `03 00 00 00` is `0x3`, and
`fb 4f 52 e4` is `0xE4524FFB`. That's our header.

The symbols and their final addresses, sorted:

```
$ i686-elf-nm -n build/kernel.elf
001000a0 T kmain
001000e0 T _start
001000ec t _start.hang
00102000 b stack_bottom
00106000 B kernel_end
00106000 b stack_top
```

And the relocations have been applied. The same four instructions we saw in
the object file now have real values:

```
$ i686-elf-objdump -d -M intel build/kernel.elf
001000e0 <_start>:
  1000e0:   bc 00 60 10 00          mov    esp,0x106000
  1000e5:   53                      push   ebx
  1000e6:   50                      push   eax
  1000e7:   e8 b4 ff ff ff          call   1000a0 <kmain>
```

`mov esp` now loads `0x106000`, the real address of `stack_top`. The `call`
contains `0xffffffb4`, which is -76 as a signed number: the distance from
the next instruction (`0x1000ec`) back to `kmain` (`0x1000a0`), since
`0x1000ec - 76 = 0x1000a0`.

Finally, the program headers, which are what the loader actually follows:

```
$ i686-elf-readelf -l build/kernel.elf
Entry point 0x1000e0
  Type     Offset   VirtAddr   PhysAddr   FileSiz MemSiz  Flg Align
  LOAD     0x001000 0x00100000 0x00100000 0x0103d 0x0103d R E 0x1000
  LOAD     0x001000 0x00102000 0x00102000 0x00000 0x04000 RW  0x1000
```

The entry point is `0x1000e0`, the address of `_start`. The first `LOAD`
line tells the loader: "copy `0x103d` bytes from file offset `0x1000` to
address `0x100000`". That covers `.text` and `.rodata`. The second line is
the `.bss`: its size in the file (`FileSiz`) is 0, but its size in memory
(`MemSiz`) is `0x4000`. The loader handles that by filling `0x4000` bytes
at `0x102000` with zeros. That's how our 16 KiB stack exists in memory
without taking up space in the file.

---

## 9. The Makefile

The Makefile is a build tool, not part of the operating system, so you can
copy it from [`code/Makefile`](code/Makefile). It's still worth knowing
what each part does, because you'll change it in later lessons.

### Finding the source files

```make
C_SRCS   := $(shell find kernel -name '*.c')
ASM_SRCS := $(shell find kernel -name '*.asm')
OBJS     := $(C_SRCS:%.c=$(BUILD)/%.o) $(ASM_SRCS:%.asm=$(BUILD)/%.asm.o)
```

Every `.c` and `.asm` file under `kernel/` is part of the kernel. Each one
is compiled to a `.o` file of the same name under `build/`. So when you add
a new source file in a later lesson, you don't need to edit the Makefile.

### The compiler flags

```make
CFLAGS  := -std=gnu11 -ffreestanding -fno-stack-protector -fno-pic \
           -O2 -g -Wall -Wextra -Ikernel
```

- **`-ffreestanding`** tells GCC that the program runs without an operating
  system or a C library. The C standard calls this a "freestanding
  environment". In it, you can only rely on a few headers that contain
  nothing but types and macros, and that the compiler itself provides:
  `<stdint.h>`, `<stddef.h>`, `<stdarg.h>`, `<stdbool.h>` and `<limits.h>`.
  Everything else, including `printf`, `strlen` and `malloc`, you write
  yourself. The flag also stops GCC from assuming that a function named,
  say, `strlen` behaves like the standard one.
- **`-fno-stack-protector`**: by default, some GCC builds add checks
  against stack buffer overflows, which call a function from the C library
  when they fail. We don't have that function, so we turn the checks off.
- **`-fno-pic`**: "position-independent code" is code that works at any
  address. Shared libraries need it. Our kernel always runs at the address
  we chose in the linker script, so we don't, and turning it off produces
  simpler code.
- **`-O2 -g`**: optimize the code, and also include debugging information
  so GDB can show source lines and variable names.
- **`-Wall -Wextra`**: turn on most warnings. In kernel code, a warning is
  very often a real bug.
- **`-Ikernel`**: look for `#include "..."` files starting from the
  `kernel/` folder. From lesson 1, includes are written like
  `#include "dev/vga.h"`, so you always know which folder a header is in.

### Linking

```make
LDFLAGS := -T $(LDSCRIPT) -nostdlib -ffreestanding -O2 -g

$(KERNEL): $(OBJS) $(LDSCRIPT)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) -lgcc
```

We call the linker through `i686-elf-gcc`, which passes the right options
on to `i686-elf-ld`. **`-T linker.ld`** uses our linker script. **`-nostdlib`**
tells it not to add the C library or the usual startup code that would
normally run before `main`. **`-lgcc`** adds `libgcc`, a small library of
helper functions that GCC calls behind your back for operations the CPU
can't do in one instruction. For example, dividing two 64-bit numbers on a
32-bit CPU becomes a call to a function called `__udivdi3`. It has no
operating system dependencies, so it's safe to use in a kernel.

### Running QEMU

```make
QEMUFLAGS := -m 128M -serial stdio -no-reboot -no-shutdown

run: $(KERNEL)
	$(QEMU) $(QEMUFLAGS) -kernel $(KERNEL)
```

- **`-m 128M`** gives the emulated PC 128 MiB of RAM.
- **`-serial stdio`** connects COM1 to the terminal, as explained in
  section 6.
- **`-no-reboot`**: when the CPU hits an error it can't recover from, it
  resets the machine (lesson 4 explains why). Without this flag, QEMU would
  reboot in an endless loop, too fast to see anything. With it, QEMU stops.
- **`-no-shutdown`**: when the machine stops, keep the QEMU window open so
  you can still read the screen.
- **`-kernel`**: use QEMU's built-in Multiboot loader to load our file.

The other targets are variations on this: `run-nox` shows no window (only
the serial port, in the terminal), `run-test` is like `run-nox` but
suitable for scripts, and `debug` starts QEMU paused, waiting for a
debugger (section 11).

---

## 10. Running it

```sh
make run
```

A QEMU window opens, and the message appears in the terminal you ran
`make` from:

```
Hello, world from toyos!
```

In the QEMU window, you'll see the same message in the top-left corner,
but mixed with text that was already on the screen. That's because the
BIOS printed its own messages before our kernel ran, and our `say` only
overwrites the cells it writes to. Here's what the top of the screen
contains, read out of the VGA memory:

```
row 0 |Hello, world from toyos!7.0-0-gb52ca86e094d-prebuilt.qemu.org)
row 1 |
row 2 |
row 3 |iPXE (http://ipxe.org) 00:03.0 CA00 PCI2.10 PnP PMM+06FD1E60+06F31E60 CA00
  ...
row 7 |Booting from ROM...
```

Row 0 used to say `SeaBIOS (version 1.17.0-0-gb52ca86e094d-prebuilt.qemu.org)`.
We wrote over its first 24 cells. Lesson 1 clears the screen first.

To stop QEMU, close its window. With `make run-nox` there's no window;
press `Ctrl-A`, then `X`.

---

## 11. Debugging tools you'll need from lesson 3 on

So far, if something goes wrong, you'll see nothing at all, or the machine
will reset. Two tools let you see what's happening inside. Learn them now,
while the kernel is small, because in lessons 3 to 8 you'll need them
often.

### GDB: stepping through the kernel

QEMU can act as a target for GDB, so you can debug the kernel like a normal
program: set breakpoints, step, and look at registers and memory. You need
two terminals:

```sh
make debug     # terminal 1: QEMU starts paused, waiting for GDB
make gdb       # terminal 2: GDB connects to QEMU
```

Then, in GDB:

```
(gdb) break _start           stop at our first instruction
(gdb) continue               run until then
(gdb) info registers         show all registers
(gdb) break kmain
(gdb) continue
(gdb) x/3xw $esp             show 3 words (4 bytes each) at the top of the stack
(gdb) next                   run one line of C
(gdb) stepi                  run one machine instruction
(gdb) x/8xh 0xb8000          show 8 half-words (2 bytes each) of VGA memory
(gdb) layout asm             show the disassembly (Ctrl-X then A to leave)
```

This is exactly how the register values and the stack picture in section 5
were obtained. Here's the real output at the `kmain` breakpoint:

```
Breakpoint 2, kmain (magic=732803074, mbi_phys=38144) at kernel/kmain.c:51
(gdb) x/3xw $esp
0x105ff4:       0x001000ec      0x2badb002      0x00009500
```

GDB shows `magic` in decimal: 732803074 is `0x2BADB002`, and 38144 is
`0x9500`.

### QEMU's interrupt log: when the machine resets

The most confusing failure in early kernel work is a machine that simply
resets, with no message. This happens when the CPU hits an error (called
an **exception**, such as dividing by zero) and there's nothing in place to
handle it. The CPU then raises a second exception, a **double fault**, and
when that can't be handled either, it gives up and resets the machine.
That is called a **triple fault**. Lesson 4 explains this chain and
installs handlers so that it can't happen any more. Until then, QEMU can
log every exception for you.

In the Makefile, there's a line that is commented out:

```make
# QEMUFLAGS += -d int,cpu_reset -D $(BUILD)/qemu.log
```

Remove the `#` and QEMU writes every exception, with all the registers, to
`build/qemu.log`. Exercise 3 below walks you through reading it.

---

## 12. Exercises

1. **Change the color.** Make the message green on black (green is color
   2). Which byte of each cell did you change, and to what value?

2. **Remove the stack setup.** Delete the `mov esp, stack_top` line,
   rebuild and run. The kernel probably still works, because the loader's
   `ESP` (`0x6f10`) happens to point to usable memory. That's exactly the
   danger: it works by accident, until it doesn't. Put the line back.

3. **Read a crash.** Enable the QEMU log (section 11), then make `kmain`
   divide by zero after printing:

   ```c
   volatile int zero = 0;
   volatile int r = 100 / zero;
   ```

   Run `make run`. Nothing new appears, and QEMU stops. Open
   `build/qemu.log` and look for lines containing `v=`. The number after
   `v=` is the exception number:

   ```
        0: v=00 e=0000 i=0 cpl=0 IP=0008:001000d2 pc=001000d2 SP=0010:00105fd8 ...
        1: v=08 e=0000 i=0 cpl=0 IP=0008:001000d2 pc=001000d2 SP=0010:00105fd8 ...
   check_exception old: 0x8 new 0xd
   Triple fault
   ```

   `v=00` is exception 0, "divide error". There's no handler, so the CPU
   raises `v=08`, the double fault. That fails too, and the log says
   `Triple fault`: the machine resets (and `-no-reboot` makes QEMU stop).
   The `pc=001000d2` tells you which instruction failed. Turn that address
   into a source line:

   ```
   $ i686-elf-addr2line -f -e build/kernel.elf 0x1000d2
   kmain
   .../kernel/kmain.c:58
   ```

   Why `volatile`? If you write `int zero = 0; ... 100 / zero` without it,
   GCC sees the division by a constant zero and may compile something
   other than a `div` instruction. We tried `1 / zero` with a `volatile`
   divisor and GCC replaced the division with a comparison, because the
   result of `1 / x` can only be 1, -1 or 0: no exception at all. When you
   want the CPU to do something specific, check the disassembly
   (`i686-elf-objdump -d -M intel build/kernel.elf`) to see what the
   compiler actually produced.

4. **Find the code.** Run `i686-elf-objdump -d -M intel build/kernel.elf`
   and find `_start` and `kmain`. Check that `_start` matches what you
   wrote, and find the `out` instruction inside `kmain`.

5. **Break the header.** Change `MB_MAGIC` to `0x1BADB003`, rebuild, and
   run. What does QEMU say? Then put it back and instead remove the
   `*(.multiboot)` line from the linker script. Run `i686-elf-objdump -h`
   to see where the `.multiboot` section went. Is it still within the
   first 8 KiB of the file?

   <details>
   <summary>What you should see</summary>

   With the wrong magic number, QEMU refuses to start the kernel:

   ```
   qemu-system-i386: Error loading uncompressed kernel without PVH ELF Note
   ```

   The message is confusing, but it means: "I didn't find a Multiboot
   header, and this file isn't any other kind of kernel I know how to
   boot" (PVH is another boot standard QEMU supports).

   Without `*(.multiboot)` in the linker script, the linker still keeps
   the section, but it decides on its own where to put it. A section that
   the script doesn't mention is called an **orphan section**, and the
   linker places it next to sections that look similar. Here it ends up
   after `.rodata`:

   ```
     2 .multiboot    0000000c  00101040  00101040  00002040  2**2
   ```

   Its file offset is `0x2040` = 8256 bytes, just past the 8 KiB (8192
   bytes) the loader searches. QEMU gives the same error as above. If the
   kernel were a little smaller, the header might happen to land inside
   the first 8 KiB and everything would work, until the kernel grew. That's
   why we place it explicitly.

   </details>

---

## Pitfalls

- **`i686-elf-gcc: command not found`**: Homebrew isn't on the `PATH` in
  that terminal. Run `eval "$(/opt/homebrew/bin/brew shellenv)"`.
- **`Error loading uncompressed kernel without PVH ELF Note`**: this is
  how QEMU says it found no valid Multiboot header. The header is missing,
  beyond the first 8 KiB of the file, misaligned, or its checksum is
  wrong. Check with `objdump -h` and `xxd` as in section 8.
- **QEMU complains about a 64-bit image**: the kernel was built with the
  wrong compiler or assembler options. Use `i686-elf-gcc` and
  `nasm -f elf32`.
- **Nothing on screen but the serial output works**: the VGA pointer isn't
  `volatile`, or the color byte is `0x00`, which is black on black.
- **Nothing at all, QEMU just sits there**: check the load address with
  `readelf -l`. If it isn't `0x00100000`, the linker script wasn't used.

## References

- The Multiboot specification, sections 3.1 (the header) and 3.2 (the
  machine state): <https://www.gnu.org/software/grub/manual/multiboot/multiboot.html>
- OSDev wiki, "Bare Bones": <https://wiki.osdev.org/Bare_Bones>. A similar
  first kernel, explained differently.
- OSDev wiki, "Printing To Screen" and "Serial Ports".
- The GNU linker manual, "Linker Scripts":
  <https://sourceware.org/binutils/docs/ld/Scripts.html>
- GCC manual, "Extended Asm":
  <https://gcc.gnu.org/onlinedocs/gcc/Extended-Asm.html>
