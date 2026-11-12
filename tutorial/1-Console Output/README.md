# Lesson 1 — Console Output

In lesson 0 the kernel printed one fixed line, using a function that could
only write from the top-left corner of the screen. In this lesson you build
the tool you'll use more than any other for the rest of the project: a
proper text console and a `printf` for the kernel, called `kprintf`.

When you're done, `make run` clears the screen and prints this, both in the
QEMU window and in your terminal:

```
toyos booting...
kprintf test: 42 -17 deadbeef C0FFEE "str" 'x' 100%
padding: [0000beef] [42   ] [   ab] [-2147483648]

PANIC: nothing more to do (test 1)
```

Why spend a whole lesson on printing? Because from now on, almost every
time something doesn't work, the first thing you'll do is print values:
the address that caused a crash, the contents of a register, the state of a
data structure. A kernel has no debugger built in and no operating system
to report errors for it. `kprintf` is how it tells you what's going on, so
it must be reliable, and it must work when nothing else does.

You'll write seven small files. Each step below ends with something you can
build and run, so you always know you're on track. The reference code for
the end of the lesson is in [`code/`](code/). To see every change compared
to lesson 0:

```sh
cd tutorial
diff -ru "0-Environment and Hello World/code" "1-Console Output/code"
```

**Contents**

1. [What's wrong with lesson 0's printing](#1-whats-wrong-with-lesson-0s-printing)
2. [The plan: a few small layers](#2-the-plan-a-few-small-layers)
3. [Step 1: a header for port I/O](#3-step-1-a-header-for-port-io)
4. [Step 2: string functions](#4-step-2-string-functions)
5. [Step 3: the serial port, properly](#5-step-3-the-serial-port-properly)
6. [Step 4: the screen](#6-step-4-the-screen)
7. [Step 5: the console](#7-step-5-the-console)
8. [Step 6: kprintf](#8-step-6-kprintf)
9. [Step 7: panic and assert](#9-step-7-panic-and-assert)
10. [Putting it together](#10-putting-it-together)
11. [Exercises](#11-exercises)

---

## 1. What's wrong with lesson 0's printing

Lesson 0's `say()` function was good enough to prove the kernel runs, but
it has several problems that would hurt quickly:

- **It always writes from the top-left corner of the screen.** The
  character at position `i` of the string goes into screen cell `i`. A
  second call to `say` overwrites the first message instead of appearing
  after it.
- **A newline doesn't move to the next line on the screen.** It's simply
  skipped on the VGA side.
- **Nothing happens when the screen is full.** Text beyond the 25th row
  would be written into memory past the end of the screen.
- **The screen is never cleared**, so our text is mixed with what the BIOS
  printed before us.
- **The blinking cursor stays wherever the BIOS left it.**
- **It can only print fixed strings.** There's no way to print a number,
  and printing numbers (addresses, sizes, counters) is the main thing a
  kernel's debug output does.
- **The serial port is used without being configured.** QEMU doesn't mind,
  but real hardware would.

---

## 2. The plan: a few small layers

We could fix all of that in one big function, but it's much easier to
write, test and change if each part has one job. We'll split printing into
layers, where each layer only knows about the one directly below it:

```
   kprintf("x = %d\n", x)          lib/kprintf.c
           │                       Turns a format string and values into
           │                       characters. Knows nothing about devices.
           │  one character at a time
           ▼
   console_putc(c)                 dev/console.c
           │                       "The console": sends each character to
           │                       every output device we have.
     ┌─────┴──────┐
     ▼            ▼
 vga_putc(c)   serial_putc(c)      dev/vga.c, dev/serial.c
                                   Each one drives one piece of hardware.
                                   Knows nothing about formatting.
```

`kprintf` doesn't know that a screen exists, and the screen driver doesn't
know what `%d` means. The benefit shows up later: in lesson 18 a terminal
driver will sit on top of `console_putc` too, and nothing below it will
have to change. If we ever add a third output device, only `console.c`
changes.

Alongside those, we need a few helpers:

- `arch/i386/io.h`: the `inb`/`outb` functions, shared by every driver.
- `lib/string.c`: `memcpy`, `strlen` and friends, since there's no C library.
- `lib/panic.c`: a function that prints an error message and stops the
  machine, for when something goes badly wrong.

Here's where everything goes:

```
kernel/
├── kmain.c                 calls console_init(), then prints
├── arch/i386/
│   ├── boot.asm            unchanged
│   ├── linker.ld           unchanged
│   └── io.h                NEW: inb, outb, inw, outw, io_wait
├── dev/
│   ├── serial.c, .h        NEW: serial_init, serial_putc
│   ├── vga.c, .h           NEW: vga_init, vga_putc
│   └── console.c, .h       NEW: console_init, console_putc
└── lib/
    ├── string.c, .h        NEW: memset, memcpy, memmove, memcmp, strlen, ...
    ├── kprintf.c, .h       NEW: kprintf, kvprintf, ksnprintf
    └── panic.c, .h         NEW: panic, and the assert macro
```

Folders are grouped by kind: `arch/i386/` holds anything specific to the
x86 CPU, `dev/` holds device drivers, and `lib/` holds general-purpose code
that doesn't touch hardware. Includes are written relative to `kernel/`,
for example `#include "dev/vga.h"`. That works because the Makefile passes
`-Ikernel` to the compiler (lesson 0, section 9).

---

## 3. Step 1: a header for port I/O

In lesson 0, `inb` and `outb` were defined inside `kmain.c`. Now two
drivers need them (the serial port and the screen's cursor), and many more
will later. So we move them to a header, `kernel/arch/i386/io.h`.

Functions defined in a header must be `static inline`. A normal function
defined in a header would be compiled into *every* `.c` file that includes
the header, and the linker would complain that the function is defined
several times. `static` makes each copy private to its file, and `inline`
asks the compiler to paste the body at each call. For a one-instruction
function, that's exactly what we want.

The header also needs an **include guard**, so that including it twice in
the same file (which happens easily when headers include other headers)
doesn't define everything twice:

```c
#ifndef ARCH_IO_H
#define ARCH_IO_H
...
#endif
```

While you're at it, add three more functions that later lessons need:

- `outw(port, value)` and `inw(port)` move 16 bits at a time instead of 8.
  Their inline assembly is the same as `outb`/`inb`, with `outw`/`inw` and
  `uint16_t` instead of `uint8_t`. The disk driver in lesson 14 reads the
  disk 16 bits at a time.
- `io_wait()` waits for about a microsecond. Some old chips need a short
  pause between two commands, and lesson 5 talks to one of them. The usual
  trick is to write to port `0x80`, which no device uses: the write itself
  takes about a microsecond, which is the delay we want.

**Check:** remove `inb`/`outb` from `kmain.c`, include `"arch/i386/io.h"`
instead, and run `make run`. You should get the same output as in lesson 0.

<details>
<summary>Reference: <code>arch/i386/io.h</code></summary>

```c
/* io.h -- x86 port I/O. */
#ifndef ARCH_IO_H
#define ARCH_IO_H

#include <stdint.h>

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

static inline void outw(uint16_t port, uint16_t val)
{
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port)
{
    uint16_t val;
    __asm__ volatile ("inw %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

/* A write to unused port 0x80 takes ~1us: gives old chips time to settle. */
static inline void io_wait(void)
{
    outb(0x80, 0);
}

#endif
```

</details>

---

## 4. Step 2: string functions

### Why the kernel needs its own

In a normal C program, `strlen`, `memcpy` and `memset` come from the C
library. The kernel isn't linked with a C library (we passed `-nostdlib`),
so if we want these functions, we write them. We'll need `strlen` in
`kprintf`, and many of the others in later lessons.

There's a second, less obvious reason. **GCC itself generates calls to
`memcpy` and `memset`, even if you never write them in your code.** When
you copy a large structure with `=`, or zero a large array, GCC may decide
the fastest way is to call `memcpy` or `memset`. It assumes these functions
always exist, even with `-ffreestanding`. (The GCC manual says so: a
freestanding program must provide `memcpy`, `memmove`, `memset` and
`memcmp`.)

Here's a real example. This function copies the whole screen (4000 bytes)
into a global variable:

```c
struct screen { unsigned short cells[2000]; };
struct screen saved;
void save_screen(void) { saved = *(struct screen *)0xB8000; }
```

There's no function call in that code. But look at what GCC produced:

```
$ i686-elf-objdump -dr build/kernel/copy.o
  15:   e8 fc ff ff ff          call   16 <save_screen+0x16>
                        16: R_386_PC32  memcpy
```

It calls `memcpy`. Without a `memcpy` in the kernel, the link fails:

```
kernel/copy.c:3:(.text+0x16): undefined reference to `memcpy'
collect2: error: ld returned 1 exit status
```

So these four functions must exist from now on, whether you call them or
not.

### What to write

Create `kernel/lib/string.h` with the prototypes and `kernel/lib/string.c`
with the code. Use the standard names and signatures, so GCC's calls find
them. The include `<stddef.h>` gives you `size_t`.

| Function | What it does |
|---|---|
| `void *memset(void *dst, int c, size_t n)` | Sets `n` bytes at `dst` to the value `c`. Returns `dst`. |
| `void *memcpy(void *dst, const void *src, size_t n)` | Copies `n` bytes from `src` to `dst`. The two areas must not overlap. Returns `dst`. |
| `void *memmove(void *dst, const void *src, size_t n)` | Like `memcpy`, but correct even when the areas overlap. |
| `int memcmp(const void *a, const void *b, size_t n)` | Compares `n` bytes. Returns 0 if equal, otherwise the difference between the first two bytes that differ. |
| `size_t strlen(const char *s)` | Number of characters before the terminating `'\0'`. |
| `int strcmp(const char *a, const char *b)` | Compares two strings. 0 if equal. |
| `int strncmp(const char *a, const char *b, size_t n)` | Same, but looks at most at `n` characters. |
| `char *strncpy(char *dst, const char *src, size_t n)` | Copies at most `n` characters, and fills the rest of `dst` with `'\0'` up to `n`. |

Simple loops that handle one byte at a time are completely fine. They're
slower than what a real C library does, but the kernel rarely copies large
amounts of memory.

Two of these deserve a closer look.

**`memmove` and overlapping areas.** Suppose you want to move 4 bytes from
address 100 to address 102, so the areas 100–103 and 102–105 overlap. If
you copy front to back, the first two copies overwrite bytes 102 and 103
before you've read them:

```
before:          100 101 102 103 104 105
                  A   B   C   D   .   .

front to back:   copy 100→102:  A B A D . .     (C is lost)
                 copy 101→103:  A B A B . .     (D is lost)
                 copy 102→104:  A B A B A .     wrong: should be C
                 copy 103→105:  A B A B A B     wrong: should be D

back to front:   copy 103→105:  A B C D . D
                 copy 102→104:  A B C D C D
                 copy 101→103:  A B C B C D
                 copy 100→102:  A B A B C D     correct: A B C D at 102
```

The rule is: when the destination is *after* the source, copy from the end
backwards; otherwise copy from the start forwards. That's all `memmove`
does differently from `memcpy`. The screen driver uses this kind of
overlapping copy when it scrolls.

**Comparing as `unsigned char`.** `strcmp` and `memcmp` must compare bytes
as `unsigned char`. If you compare plain `char`s, which are signed on x86,
a byte like `0xE9` (é in Latin-1) counts as negative and sorts *before*
`'a'`. The C standard says the comparison is done on unsigned values.

### A compiler option to know about

There's one trap in writing `memset` yourself. GCC can recognize a loop
that fills memory byte by byte, and replace the loop with a call to
`memset`, because a library `memset` is usually faster. If it did that
*inside* your `memset`, the function would call itself forever. Many
versions of GCC have done this. The GCC version used to write this
tutorial (16.2) is careful not to do it inside a function named `memset`,
but to be safe regardless of the version, the reference Makefile adds this
option to `CFLAGS`:

```make
-fno-tree-loop-distribute-patterns
```

It tells GCC never to turn a loop into a call to `memset` or `memcpy`. Add
it to your Makefile too. (It doesn't stop GCC from calling `memcpy` for a
structure copy, as above. That's a different mechanism, which is fine.)

**Check:** `make`. Nothing calls these functions yet, but they must compile
without warnings.

<details>
<summary>Reference: <code>lib/string.c</code></summary>

```c
/* string.c -- simple byte-at-a-time versions. Fast enough for a toy kernel. */
#include "lib/string.h"

void *memset(void *dst, int c, size_t n)
{
    unsigned char *d = dst;
    while (n--)
        *d++ = (unsigned char)c;
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--)
        *d++ = *s++;
    return dst;
}

/* Like memcpy, but correct when the two ranges overlap. */
void *memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    if (d < s) {
        while (n--)
            *d++ = *s++;
    } else {
        while (n--)                 /* copy backwards */
            d[n] = s[n];
    }
    return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = a, *y = b;
    for (; n; n--, x++, y++)
        if (*x != *y)
            return *x - *y;
    return 0;
}

size_t strlen(const char *s)
{
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) {
        if (*a != *b)
            return (unsigned char)*a - (unsigned char)*b;
        if (*a == '\0')
            return 0;
    }
    return 0;
}

/* Copies at most n bytes and pads with NULs, exactly like the standard one. */
char *strncpy(char *dst, const char *src, size_t n)
{
    size_t i = 0;
    for (; i < n && src[i]; i++)
        dst[i] = src[i];
    for (; i < n; i++)
        dst[i] = '\0';
    return dst;
}
```

The header [`lib/string.h`](code/kernel/lib/string.h) just lists the eight
prototypes, with `#include <stddef.h>` and an include guard.

</details>

---

## 5. Step 3: the serial port, properly

Lesson 0 explained what the serial port is: a UART chip, controlled
through 8 I/O ports starting at `0x3F8` for COM1, which QEMU connects to
your terminal. We used two of its registers, Data and Line Status, to send
bytes. Now we'll configure it properly before using it, the way real
hardware requires.

### What needs configuring

Two machines talking over a serial line must agree on two things, because
the wire just carries a sequence of high and low voltages and there's
nothing in it that says how to interpret them.

**The speed.** The speed of a serial line is measured in **baud**, which
for our purposes means bits per second. Both ends must use the same speed,
or the receiver samples the wire at the wrong moments and reads garbage.
Common speeds are 9600, 38400 and 115200 baud.

**The format of each byte.** Each byte is sent as a small frame: a
**start bit** that says "a byte is coming", then the **data bits**, then
optionally a **parity bit** (a simple error check), then one or two
**stop bits** that mark the end. The most common format, which we'll use,
is called **8N1**: 8 data bits, No parity, 1 stop bit. With the start bit,
that's 10 bits on the wire for each byte, so at 38400 baud the line carries
3840 bytes per second.

QEMU's emulated UART ignores all of this (it hands each byte to your
terminal instantly), but setting it up correctly costs nothing and makes
the driver work on a real PC.

### How the UART is configured

The UART has more registers than it has ports, so some ports have two
meanings. Here are the registers we need, as offsets from `0x3F8`:

| Offset | Name | What it controls |
|---|---|---|
| +0 | Data | Writing sends a byte, reading returns a received byte. |
| +1 | Interrupt Enable | Which events make the UART raise an interrupt. |
| +2 | FIFO Control | The UART's internal queues (explained below). |
| +3 | Line Control | The byte format (8N1), plus the DLAB bit (explained below). |
| +4 | Modem Control | Signal wires to the other end of the line. |
| +5 | Line Status | The UART's state, such as "ready to send". |

**Setting the speed.** Inside the UART, a clock runs at a fixed rate, and
the UART derives its speed from it by dividing: speed = 115200 / divisor.
So the divisor 1 gives 115200 baud, 3 gives 38400 baud, and 12 gives 9600
baud. The divisor is a 16-bit number, stored in two 8-bit registers.

But there are no free ports for those two registers. The designers solved
this by reusing ports +0 and +1. Bit 7 of the Line Control register is a
switch called **DLAB** ("divisor latch access bit"). While DLAB is 1,
ports +0 and +1 give access to the low and high bytes of the divisor
instead of to the Data and Interrupt Enable registers. So setting the speed
is a three-step dance: turn DLAB on, write the divisor, turn DLAB off.

**The other settings:**

- **Interrupt Enable = `0x00`.** An **interrupt** is a signal from a device
  that makes the CPU stop what it's doing and run a handler. We haven't
  set up any handlers yet (that's lessons 4 and 5), so we tell the UART
  not to send any. We'll check its state ourselves when we need to.
- **Line Control = `0x03`.** Bits 0–1 set the number of data bits: `11` in
  binary means 8. Bit 2 is the number of stop bits: 0 means 1. Bits 3–5
  set the parity: all 0 means none. Bit 7 is DLAB, which is 0 again. So
  `0x03` means 8N1, DLAB off.
- **FIFO Control = `0xC7`.** A **FIFO** ("first in, first out") is a queue.
  The 16550 has two 16-byte queues, one for sending and one for receiving,
  so the CPU can hand over several bytes at once instead of waiting for
  each byte to go out. `0xC7` is `1100 0111` in binary: bit 0 turns the
  queues on, bits 1 and 2 empty them, and bits 6–7 set when the UART
  signals "data received" (after 14 bytes, which matters only once we use
  interrupts).
- **Modem Control = `0x0B`.** This is `0000 1011` in binary. Bits 0 and 1
  turn on two signal wires called **DTR** ("data terminal ready") and
  **RTS** ("request to send"). On a real cable, they tell the other end
  "I'm here and ready". Bit 3 turns on a wire called **OUT2**. On a PC,
  OUT2 is wired so that it connects the UART's interrupt signal to the
  rest of the machine: if it's off, the UART's interrupts never reach the
  CPU. We don't use interrupts yet, but lesson 18 does, and this is the
  kind of setting that's very hard to find when it's missing.

Here's the whole setup, in order:

```
port      value   meaning
0x3F9     0x00    no interrupts
0x3FB     0x80    DLAB = 1: ports 0x3F8 and 0x3F9 now hold the divisor
0x3F8     0x03    divisor, low byte  = 3
0x3F9     0x00    divisor, high byte = 0       → 115200 / 3 = 38400 baud
0x3FB     0x03    8 data bits, no parity, 1 stop bit; DLAB = 0 again
0x3FA     0xC7    turn on and empty the FIFOs
0x3FC     0x0B    DTR, RTS, OUT2
```

### What to write

Create `kernel/dev/serial.c` and `serial.h` with two functions:

- **`serial_init()`** writes the seven values above. Give the register
  offsets names with `#define` (`LINE_CTRL`, `LINE_STATUS`...), so the code
  reads like the table instead of like a list of numbers.
- **`serial_putc(c)`** sends one character, exactly like lesson 0's: wait
  for bit 5 of Line Status, then write to Data. Also, if `c` is `'\n'`,
  first send `'\r'`, so that every caller gets correct line endings without
  thinking about it. (You can do that by having `serial_putc` call itself
  with `'\r'`.)

**Check:** in `kmain`, call `serial_init()`, then send a string one
character at a time with `serial_putc`. Run `make run-nox`, which shows
only the serial port in the terminal (quit with `Ctrl-A` then `X`). Your
text should appear, with each line starting at the left edge.

<details>
<summary>Reference: <code>dev/serial.c</code></summary>

```c
/* serial.c -- COM1 output. QEMU's -serial stdio connects it to your terminal. */
#include "dev/serial.h"
#include "arch/i386/io.h"

#define COM1 0x3F8

/* Register offsets from COM1 */
#define DATA        0       /* read: received byte, write: byte to send  */
#define INT_ENABLE  1
#define FIFO_CTRL   2
#define LINE_CTRL   3
#define MODEM_CTRL  4
#define LINE_STATUS 5

#define LSR_TX_EMPTY 0x20   /* transmit holding register is empty */

void serial_init(void)
{
    outb(COM1 + INT_ENABLE, 0x00);      /* no interrupts (yet)             */
    outb(COM1 + LINE_CTRL,  0x80);      /* DLAB=1: next 2 regs = divisor   */
    outb(COM1 + DATA,       3);         /* divisor 3 -> 38400 baud (low)   */
    outb(COM1 + INT_ENABLE, 0);         /*                         (high)  */
    outb(COM1 + LINE_CTRL,  0x03);      /* 8 data bits, no parity, 1 stop  */
    outb(COM1 + FIFO_CTRL,  0xC7);      /* enable and clear FIFOs          */
    outb(COM1 + MODEM_CTRL, 0x0B);      /* DTR, RTS, OUT2 (OUT2 gates IRQs) */
}

void serial_putc(char c)
{
    if (c == '\n')
        serial_putc('\r');              /* terminals want CR LF */
    while ((inb(COM1 + LINE_STATUS) & LSR_TX_EMPTY) == 0)
        ;
    outb(COM1 + DATA, (uint8_t)c);
}
```

</details>

---

## 6. Step 4: the screen

Lesson 0 explained VGA text mode: the screen is 25 rows of 80 cells,
stored in memory at `0xB8000`, 2 bytes per cell (the character, then the
color), and the cell at row `r`, column `c` is number `r * 80 + c`. Now
we'll write a driver that behaves like a terminal: text continues where it
left off, newlines work, and the screen scrolls when it's full.

### Remembering where we are

The key difference from lesson 0 is that the driver **remembers the
current position**. Keep two variables in `vga.c`:

```c
static int row, col;
```

They point to the cell where the next character will go. `static` at file
level makes them private to `vga.c`: no other file can change them by
accident. Printing an ordinary character means writing it into cell
`row * 80 + col`, then moving one column to the right.

For the color, use `0x07` (light grey on black), which is what a text
console traditionally looks like. Bright white (`0x0F`) gets tiring to
read.

### Special characters

A few characters don't print anything but move the position instead:

| Character | Name | What the driver does |
|---|---|---|
| `'\n'` | newline | Go to the start of the next line: `row++`, `col = 0`. |
| `'\r'` | carriage return | Go back to the start of the current line: `col = 0`. |
| `'\b'` | backspace | Move one column left, if not already at column 0. The terminal driver in lesson 18 erases a character by printing `"\b \b"`: back, overwrite with a space, back again. |
| `'\t'` | tab | Move to the next column that is a multiple of 8. |

For the tab, the next multiple of 8 after `col` is `(col + 8) & ~7`.
`~7` is a number with every bit set except the lowest three, so `& ~7`
clears the lowest three bits, which rounds down to a multiple of 8. Adding
8 first makes sure we always move forward. For example, with `col = 13`:
`13 + 8 = 21`, and 21 rounded down to a multiple of 8 is 16. With
`col = 16`: `24`, which is already a multiple of 8, so the tab goes to
column 24.

### Wrapping and scrolling

After handling a character, two checks keep the position on the screen:

1. If `col` reached 80, the line is full: go to column 0 of the next row.
2. If `row` reached 25, we've gone past the bottom of the screen: scroll.

**Scrolling** means moving every line up by one. The top line disappears,
and the bottom line becomes empty, ready for new text:

```
before scrolling                      after scrolling
row  0 │line A                        row  0 │line B
row  1 │line B                        row  1 │line C
  ...  │...                             ...  │...
row 23 │line X                        row 23 │line Y
row 24 │line Y                        row 24 │                (blank)
       (row would be 25: off screen)  (row = 24, col = 0)
```

In memory, that's a copy: cells 80 to 1999 (rows 1–24) move to cells 0
to 1919 (rows 0–23). Then fill cells 1920 to 1999 (row 24) with spaces,
and set `row` back to 24. Note that the source and the destination overlap,
which is the situation `memmove` exists for. You can either call `memmove`
or write a loop that copies from the first cell upwards; copying upwards is
safe here because each cell is read before it gets overwritten (the
destination is *before* the source).

### Clearing the screen

`vga_init()` fills all 2000 cells with a space in our color, and sets `row`
and `col` to 0. That gets rid of the BIOS's text.

### Moving the blinking cursor

The blinking underline on the screen is the **hardware cursor**. It's
drawn by the graphics card itself, at a position the card stores
internally. It has nothing to do with where *we* write characters: writing
to cell 250 doesn't move it. If we don't tell the card where the cursor
should be, it stays where the BIOS left it, which looks wrong and is
confusing once you start typing in lesson 18.

The cursor position is stored in the graphics card's **CRT controller**,
the part of the card that produces the picture (the name dates from
cathode-ray-tube monitors). The CRT controller has about 25 registers, but
it's only reachable through **two** I/O ports. This is a very common design
in PC hardware, called an **index/data pair**:

- port **`0x3D4`** is the **index** port: you write the *number* of the
  register you want to access;
- port **`0x3D5`** is the **data** port: you then read or write the value
  of the register you selected.

The cursor position is a cell number (0 to 1999), which needs 16 bits. It's
split across two registers: register **`0x0E`** holds the high byte and
register **`0x0F`** holds the low byte. For example, to put the cursor at
row 3, column 10:

```
cell number = 3 * 80 + 10 = 250 = 0x00FA
high byte = 0x00, low byte = 0xFA

outb(0x3D4, 0x0F);   select register 0x0F (cursor position, low byte)
outb(0x3D5, 0xFA);   write the low byte
outb(0x3D4, 0x0E);   select register 0x0E (cursor position, high byte)
outb(0x3D5, 0x00);   write the high byte
```

Write a small function `move_cursor()` that does this for the current
`row` and `col`, and call it at the end of every `vga_putc` and at the end
of `vga_init`.

### What to write

`kernel/dev/vga.c` and `vga.h`, with two public functions:

- **`vga_init()`**: clear the screen, put the position at the top-left,
  move the cursor there.
- **`vga_putc(c)`**: handle `c` (special character or normal), wrap, scroll
  if needed, move the cursor.

Inside, a few private helpers keep it readable: `put_cell(row, col, ch)`,
`scroll()` and `move_cursor()`.

**Check:** in `kmain`, call `vga_init()` and then print 30 lines with
`vga_putc` (for example, a loop that prints a letter and `'\n'` each time:
`'A' + i % 26`). Run `make run`. The screen should start empty, the lines
should scroll up so that the last 25 or so are visible, and the cursor
should blink just after the last line.

<details>
<summary>Reference: <code>dev/vga.c</code></summary>

```c
/* vga.c -- VGA text mode: 80x25 cells of (char | color << 8) at 0xB8000. */
#include <stdint.h>
#include "dev/vga.h"
#include "arch/i386/io.h"

#define VGA_MEM   ((volatile uint16_t *)0xB8000)
#define COLS      80
#define ROWS      25
#define COLOR     0x07              /* light grey on black */

/* CRT controller ports, used to move the blinking cursor */
#define CRTC_INDEX 0x3D4
#define CRTC_DATA  0x3D5

static int row, col;

static void put_cell(int r, int c, char ch)
{
    VGA_MEM[r * COLS + c] = (uint16_t)(COLOR << 8) | (uint8_t)ch;
}

static void move_cursor(void)
{
    uint16_t pos = (uint16_t)(row * COLS + col);
    outb(CRTC_INDEX, 0x0F);
    outb(CRTC_DATA, pos & 0xFF);
    outb(CRTC_INDEX, 0x0E);
    outb(CRTC_DATA, pos >> 8);
}

/* Move every line up by one and blank the last line. */
static void scroll(void)
{
    for (int i = 0; i < (ROWS - 1) * COLS; i++)
        VGA_MEM[i] = VGA_MEM[i + COLS];
    for (int c = 0; c < COLS; c++)
        put_cell(ROWS - 1, c, ' ');
    row = ROWS - 1;
}

void vga_init(void)
{
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            put_cell(r, c, ' ');
    row = col = 0;
    move_cursor();
}

void vga_putc(char ch)
{
    switch (ch) {
    case '\n': row++; col = 0;              break;
    case '\r': col = 0;                     break;
    case '\b': if (col > 0) col--;          break;
    case '\t': col = (col + 8) & ~7;        break;
    default:   put_cell(row, col, ch); col++; break;
    }
    if (col >= COLS) {
        col = 0;
        row++;
    }
    if (row >= ROWS)
        scroll();
    move_cursor();
}
```

The reference uses a loop instead of `memmove` in `scroll()`, because
`memmove`'s parameters aren't `volatile` and the screen memory is.

</details>

---

## 7. Step 5: the console

The console is the layer that ties the two devices together. It's tiny,
but it means everything above it only has to call one function:

- **`console_init()`** calls `serial_init()` and `vga_init()`.
- **`console_putc(c)`** calls `serial_putc(c)` and `vga_putc(c)`.

Put them in `kernel/dev/console.c` and `console.h`. That's the whole file
for now. In lesson 18, keyboard input and the terminal driver will make it
more interesting.

**Check:** replace the test code in `kmain` with `console_init()` and a
loop that sends a string through `console_putc`. The same text should
appear on the screen and in the terminal.

<details>
<summary>Reference: <code>dev/console.c</code></summary>

```c
/* console.c -- fans each character out to every output device. */
#include "dev/console.h"
#include "dev/serial.h"
#include "dev/vga.h"

void console_init(void)
{
    serial_init();
    vga_init();
}

void console_putc(char c)
{
    serial_putc(c);
    vga_putc(c);
}
```

</details>

---

## 8. Step 6: kprintf

Now the main event. `kprintf` works like the standard `printf`, with a
smaller set of features: the ones a kernel actually needs for debugging.

### What it supports

| Conversion | Prints | Example | Output |
|---|---|---|---|
| `%d`, `%i` | a signed integer in decimal | `kprintf("%d", -17)` | `-17` |
| `%u` | an unsigned integer in decimal | `kprintf("%u", 3000000000u)` | `3000000000` |
| `%x`, `%X` | an unsigned integer in hexadecimal, lower or upper case | `kprintf("%x %X", 255, 255)` | `ff FF` |
| `%p` | a pointer, as `0x` and 8 hex digits | `kprintf("%p", (void *)0xb8000)` | `0x000b8000` |
| `%c` | one character | `kprintf("%c", 'x')` | `x` |
| `%s` | a string | `kprintf("%s", "str")` | `str` |
| `%%` | a percent sign | `kprintf("100%%")` | `100%` |

Between the `%` and the letter, you can put:

- a **width**: the minimum number of characters to print. If the value is
  shorter, it's padded with spaces on the left. `kprintf("[%5s]", "ab")`
  prints `[   ab]`.
- the **`-` flag**: pad on the right instead (left-align).
  `kprintf("[%-5d]", 42)` prints `[42   ]`.
- the **`0` flag**: pad with zeros instead of spaces.
  `kprintf("[%08x]", 0xbeef)` prints `[0000beef]`. This is the one you'll
  use most, for addresses.
- an **`l`**, which is accepted and ignored (we'll see why below).

We also want **`ksnprintf(buf, size, fmt, ...)`**, which formats into a
buffer instead of printing, like the standard `snprintf`. It always writes
a `'\0'` at the end and never writes more than `size` bytes. Later lessons
use it to build names and messages.

### How a function with `...` gets its arguments

`kprintf` takes a variable number of arguments, which C writes as `...`.
Before writing it, it helps to see where those arguments actually are.

Remember the calling convention from lesson 0: arguments are pushed on the
stack from right to left, so they end up in memory in order, first
argument at the lowest address. That's true for the `...` arguments too.
Here's the real stack when `kmain` calls

```c
kprintf("kprintf test: %d %d %x %X \"%s\" '%c' 100%%\n",
        42, -17, 0xdeadbeef, 0xc0ffee, "str", 'x');
```

captured with GDB at the first line of `kprintf`:

```
(gdb) x/8xw $esp
0x105fd4:   0x00100cf3   0x0010111c   0x0000002a   0xffffffef
0x105fe4:   0xdeadbeef   0x00c0ffee   0x00101044   0x00000078
```

Read it one word (4 bytes) at a time:

```
address    value        what it is
0x105fd4   0x00100cf3   return address, back into kmain
0x105fd8   0x0010111c   1st argument: fmt, a pointer to the format string
0x105fdc   0x0000002a   42
0x105fe0   0xffffffef   -17  (in two's complement, -17 is 0xFFFFFFEF)
0x105fe4   0xdeadbeef   0xdeadbeef
0x105fe8   0x00c0ffee   0xc0ffee
0x105fec   0x00101044   a pointer to the string "str"
0x105ff0   0x00000078   'x' (ASCII 0x78), stored as a full 4-byte int
```

So the extra arguments are simply laid out one after the other, right
after `fmt`. To read them, `kprintf` needs a pointer that starts just after
`fmt` and moves forward by the size of each argument. That's exactly what
the standard header **`<stdarg.h>`** provides. It's one of the headers the
compiler supplies itself, so it works in the kernel:

- `va_list ap;` declares that pointer.
- `va_start(ap, fmt);` sets it to just after the last named argument,
  `fmt`.
- `va_arg(ap, int)` returns the next argument, read as an `int`, and moves
  the pointer past it.
- `va_end(ap);` is called when you're done.

Nothing in memory says what type each argument has. **The format string is
the only description**: `%d` means "the next argument is an `int`", `%s`
means "the next argument is a `char *`". If the format string and the
arguments don't match, `kprintf` reads the wrong bytes. GCC can check this
for you (see "Letting GCC check format strings" below).

Look at the last line of the stack dump: `'x'` takes a full 4 bytes. In C,
when a `char` or `short` is passed through `...`, it's automatically
converted to `int`. This is called **default argument promotion**. So for
`%c` you must read it with `va_arg(ap, int)` and convert it back to `char`.
`va_arg(ap, char)` is a bug, and GCC tells you so:

```
warning: 'char' is promoted to 'int' when passed through '...'
note: (so you should pass 'int' not 'char' to 'va_arg')
note: if this code is reached, the program will abort
```

### One formatter, two destinations

`kprintf` prints to the console, and `ksnprintf` writes to a buffer. The
formatting logic, which is the hard part, is the same for both. To avoid
writing it twice, write it once in a function that doesn't know where the
characters go. It receives a **function to call for each character**:

```c
typedef void (*putc_fn)(char c, void *ctx);

static void format(putc_fn out, void *ctx, const char *fmt, va_list ap);
```

`format` calls `out(c, ctx)` for every character it produces. The `ctx`
("context") pointer is passed through untouched, so the output function can
keep its own state there:

- `kprintf`'s output function ignores `ctx` and calls `console_putc(c)`.
- `ksnprintf`'s output function receives a pointer to a small structure
  holding the buffer, its size and the current position. It stores the
  character if there's room left, and moves the position forward.

`format` takes a `va_list` instead of `...`, so that both `kprintf` and
`ksnprintf` can call it: each one does `va_start`, passes `ap` to `format`,
then does `va_end`.

It's also useful to have **`kvprintf(fmt, ap)`**, a version of `kprintf`
that takes a `va_list`. `panic` (in the next step) needs it, to pass its
own `...` arguments on to the formatter.

### The main loop

`format` goes through the format string one character at a time:

1. If the character isn't `%`, pass it to `out` and continue.
2. If it is `%`, read what follows it, in this order:
   - any number of flag characters, `-` or `0`, and remember them;
   - any digits, which form the width (for `"%12d"`, read `1` then `2`:
     `width = 1`, then `width = 1 * 10 + 2 = 12`);
   - any `l` characters, which are skipped;
   - the conversion letter, which decides what to do with the next
     argument.
3. If the format string ends right after a `%`, stop. If the letter is
   unknown, print the `%` and the letter as they are, so a typo in a
   format string is visible instead of silently lost.

### Converting a number to text

This is the part that needs some thought. To print a number in base 10, you
need its digits. The easy way to get them is with `% 10` (the last digit)
and `/ 10` (the number without its last digit). But that gives the digits
**from right to left**. For 1234:

```
n = 1234    n % 10 = 4     n / 10 = 123
n = 123     n % 10 = 3     n / 10 = 12
n = 12      n % 10 = 2     n / 10 = 1
n = 1       n % 10 = 1     n / 10 = 0     → stop
```

The trick is to **fill a buffer from the end towards the start**, so the
digits come out in the right order without a separate reversing step:

```c
const char *digits = "0123456789abcdef";
char buf[32];
int i = sizeof buf;               /* start just past the end */

do {
    buf[--i] = digits[n % base];
    n /= base;
} while (n);

/* the number is now buf[i] .. buf[31], which is (32 - i) characters */
```

```
buf:  [ ...  |  '1' | '2' | '3' | '4' ]
               28     29     30     31
               ▲
               i = 28: the text starts here, and is 32 - 28 = 4 characters
```

The same code does hexadecimal by setting `base` to 16: `n % 16` is a value
from 0 to 15, and `digits[n % 16]` turns it into `'0'`–`'9'` or
`'a'`–`'f'`. For `%X`, use a second digit string with capital letters. For
0xBEEF: `0xBEEF % 16 = 0xF` → `'f'`, then `0xBEE % 16 = 0xE` → `'e'`, and
so on, giving `beef`.

Two details matter:

- Use **`do ... while`**, not `while`. With a plain `while (n)`, the number
  0 would produce no digits at all. `do ... while` runs the body at least
  once, so 0 prints as `"0"`.
- 32 characters is plenty: the longest number we print is 4294967295, which
  has 10 digits.

### Negative numbers, and the most negative one

For `%d`, if the value is negative, print a `-` and then the number without
its sign. The obvious way to remove the sign is `-v`, but there's a trap.

A 32-bit `int` goes from -2147483648 to 2147483647. There's one more
negative number than positive ones, so **-2147483648 has no positive
counterpart**: `-v` doesn't fit in an `int`. In C that's undefined
behavior, and in practice it gives -2147483648 back, still negative, and
the digit loop prints garbage.

The solution is to do the negation on an **unsigned** number, where the
arithmetic is defined to wrap around modulo 2³²:

```c
int v = va_arg(ap, int);
uint32_t magnitude = v < 0 ? -(uint32_t)v : (uint32_t)v;
```

For `v = -2147483648`: `(uint32_t)v` is `0x80000000`, and negating it as an
unsigned number gives 2³² - 0x80000000 = `0x80000000` = 2147483648, which is
exactly the magnitude we want. For `v = -17`: `(uint32_t)v` is
`0xFFFFFFEF`, and 2³² - 0xFFFFFFEF = 17.

So the number-printing helper works on `uint32_t` only, and takes the sign
separately.

### Padding

Once you have the text of a value (from the digit buffer, a string, or a
single character) and optionally a sign, padding it to the width is the
same for every conversion. So write one helper for it, for example:

```c
/* Print [sign] followed by s[0..len), padded to the width. */
static void emit(putc_fn out, void *ctx, char sign, const char *s, int len,
                 struct spec sp);
```

where `struct spec` holds the width and the two flags that the main loop
parsed. The number of padding characters is
`pad = width - len - (sign ? 1 : 0)`. Then there are three cases:

| Flags | Order | `%5d` of -42 with these flags |
|---|---|---|
| none | spaces, sign, text | `"  -42"` |
| `0` | sign, zeros, text | `"-0042"` |
| `-` | sign, text, spaces | `"-42  "` |

Notice that with the `0` flag, the zeros go *after* the sign: `-0042`,
not `00-42`. That's why the sign is passed separately instead of being part
of the text. If both `-` and `0` are given, `-` wins (padding with zeros on
the right would change the number).

### The remaining conversions

- **`%s`**: if the pointer is `NULL`, print `(null)` instead of crashing.
  You *will* pass a null pointer to `%s` by mistake one day, and a crash
  inside the debug output is the worst possible place for it.
- **`%p`**: print `0x`, then the value as hexadecimal with width 8 and the
  `0` flag, so all addresses have the same length and line up.
- **`%c`**: read an `int`, convert it to `char`, and print it as a
  1-character string (so the width works for it too).

### Letting GCC check format strings

Add this to the declaration of `kprintf` in `kprintf.h`:

```c
void kprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
```

It tells GCC: "argument 1 is a printf-style format string, and the values
start at argument 2". GCC then checks every call, like it does for the real
`printf`, and warns when an argument doesn't match its conversion. Do the
same for `ksnprintf` (format string is argument 3, values start at 4).

This will catch a surprise right away. With `i686-elf-gcc`, **`uint32_t`
is `unsigned long`, not `unsigned int`**. Both are 32 bits on i386, but
they're different types, so this warns:

```
warning: format '%x' expects argument of type 'unsigned int', but argument 2
has type 'uint32_t' {aka 'long unsigned int'} [-Wformat=]
```

The fix is to write `%lx` or `%lu` for `uint32_t` values. That's why our
`kprintf` accepts and ignores `l`: on i386, `long` and `int` are the same
size, so the conversion is identical, and the `l` only exists to keep GCC's
checker happy. Don't turn the warning off. It finds real bugs, like
passing a pointer where a number is expected.

### What to write

`kernel/lib/kprintf.c` and `kprintf.h`, with:

- `struct spec` (width, left flag, zero flag);
- `emit()` for padding, and `emit_num()` that converts a `uint32_t` in a
  given base and calls `emit()`;
- `format()`, the main loop;
- `kprintf()`, `kvprintf()` and `ksnprintf()`, each a few lines around
  `format()`.

Build it in stages, testing each one before moving on:

1. `%s`, `%c` and `%%` only. Print a few strings.
2. Add `%x` and `%u` with `emit_num`. Print 0, 255, 0xdeadbeef.
3. Add `%d`, then test -17, 0 and -2147483648. (Writing the literal
   `-2147483648` in C is itself tricky, because `2147483648` doesn't fit in
   an `int` before the minus is applied. Write `-2147483647 - 1`.)
4. Add the width and the flags, then `%p`.
5. Add `ksnprintf`.

**Check:** in `kmain`, after `console_init()`:

```c
kprintf("kprintf test: %d %d %x %X \"%s\" '%c' 100%%\n",
        42, -17, 0xdeadbeef, 0xc0ffee, "str", 'x');
kprintf("padding: [%08x] [%-5d] [%5s] [%d]\n", 0xbeef, 42, "ab", -2147483647 - 1);
```

must print:

```
kprintf test: 42 -17 deadbeef C0FFEE "str" 'x' 100%
padding: [0000beef] [42   ] [   ab] [-2147483648]
```

<details>
<summary>Reference: <code>lib/kprintf.c</code></summary>

```c
/* kprintf.c -- printf-style formatting.
 *
 * The core, format(), knows nothing about devices: it hands every output
 * character to a callback. kprintf() passes a callback that prints to the
 * console; ksnprintf() passes one that stores into a buffer.
 */
#include <stdint.h>
#include "lib/kprintf.h"
#include "lib/string.h"
#include "dev/console.h"

typedef void (*putc_fn)(char c, void *ctx);

struct spec {
    int width;      /* minimum field width             */
    int left;       /* '-' flag: pad on the right      */
    int zero;       /* '0' flag: pad with zeros        */
};

/* Print [sign] + s[0..len) padded to sp.width. */
static void emit(putc_fn out, void *ctx, char sign, const char *s, int len,
                 struct spec sp)
{
    int pad = sp.width - len - (sign ? 1 : 0);

    if (!sp.left && !sp.zero)
        for (; pad > 0; pad--) out(' ', ctx);
    if (sign)
        out(sign, ctx);
    if (!sp.left && sp.zero)
        for (; pad > 0; pad--) out('0', ctx);
    for (int i = 0; i < len; i++)
        out(s[i], ctx);
    for (; pad > 0; pad--)                  /* only left-aligned is left */
        out(' ', ctx);
}

static void emit_num(putc_fn out, void *ctx, uint32_t n, unsigned base,
                     int upper, char sign, struct spec sp)
{
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char buf[32];
    int i = sizeof buf;

    do {                                    /* least significant digit first */
        buf[--i] = digits[n % base];
        n /= base;
    } while (n);
    emit(out, ctx, sign, buf + i, sizeof buf - i, sp);
}

static void format(putc_fn out, void *ctx, const char *fmt, va_list ap)
{
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            out(*fmt, ctx);
            continue;
        }
        fmt++;

        struct spec sp = {0, 0, 0};
        for (;; fmt++) {
            if (*fmt == '-')      sp.left = 1;
            else if (*fmt == '0') sp.zero = 1;
            else break;
        }
        while (*fmt >= '0' && *fmt <= '9')
            sp.width = sp.width * 10 + (*fmt++ - '0');
        while (*fmt == 'l')                 /* long is the same as int on i386 */
            fmt++;

        switch (*fmt) {
        case 'c': {
            char c = (char)va_arg(ap, int);  /* char is promoted to int */
            emit(out, ctx, 0, &c, 1, sp);
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s)
                s = "(null)";
            emit(out, ctx, 0, s, strlen(s), sp);
            break;
        }
        case 'd':
        case 'i': {
            int v = va_arg(ap, int);
            /* Negate as unsigned: -INT_MIN does not fit in an int. */
            uint32_t mag = v < 0 ? -(uint32_t)v : (uint32_t)v;
            emit_num(out, ctx, mag, 10, 0, v < 0 ? '-' : 0, sp);
            break;
        }
        case 'u': emit_num(out, ctx, va_arg(ap, unsigned), 10, 0, 0, sp); break;
        case 'x': emit_num(out, ctx, va_arg(ap, unsigned), 16, 0, 0, sp); break;
        case 'X': emit_num(out, ctx, va_arg(ap, unsigned), 16, 1, 0, sp); break;
        case 'p':
            out('0', ctx);
            out('x', ctx);
            sp.width = 8;
            sp.zero = 1;
            emit_num(out, ctx, (uintptr_t)va_arg(ap, void *), 16, 0, 0, sp);
            break;
        case '%':
            out('%', ctx);
            break;
        case '\0':                          /* format ended with a lone '%' */
            return;
        default:                            /* unknown: print it as-is */
            out('%', ctx);
            out(*fmt, ctx);
            break;
        }
    }
}

/* ---- kprintf: output to the console ---- */

static void console_out(char c, void *ctx)
{
    (void)ctx;
    console_putc(c);
}

void kvprintf(const char *fmt, va_list ap)
{
    format(console_out, NULL, fmt, ap);
}

void kprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    kvprintf(fmt, ap);
    va_end(ap);
}

/* ---- ksnprintf: output to a buffer, always NUL-terminated ---- */

struct bufout {
    char  *buf;
    size_t size;
    size_t pos;
};

static void buffer_out(char c, void *ctx)
{
    struct bufout *b = ctx;
    if (b->pos + 1 < b->size)
        b->buf[b->pos++] = c;
}

void ksnprintf(char *buf, size_t size, const char *fmt, ...)
{
    struct bufout b = { buf, size, 0 };
    va_list ap;

    va_start(ap, fmt);
    format(buffer_out, &b, fmt, ap);
    va_end(ap);
    if (size > 0)
        buf[b.pos] = '\0';
}
```

In `buffer_out`, the test `b->pos + 1 < b->size` keeps one byte free for
the final `'\0'`.

</details>

---

## 9. Step 7: panic and assert

### panic

Sometimes the kernel discovers that something is so wrong that continuing
would only make things worse: a data structure is corrupted, a "can't
happen" case happened, the machine has no memory left. A user program
would exit with an error, but the kernel has nobody to exit *to*. The
traditional answer, which Unix has used since the beginning, is a function
called **`panic`**: print a message saying what went wrong, then stop the
machine. Stopping right away, with a clear message, is far better than
running on with corrupted data and crashing mysteriously later.

`panic` takes a format string and values, like `kprintf`:

```c
panic("out of memory (wanted %d bytes)", size);
```

It does three things:

1. **Disable interrupts** with the `cli` instruction. We don't use
   interrupts yet, but from lesson 5 on, an interrupt could otherwise run
   other code in the middle of the panic.
2. Print `PANIC: `, then the message (using `kvprintf` to pass the `...`
   arguments on), then a newline.
3. Stop the CPU with `hlt`, in an endless loop, like at the end of
   `_start`.

Declare it in `panic.h` with two attributes:

```c
void panic(const char *fmt, ...)
    __attribute__((noreturn, format(printf, 1, 2)));
```

`format` is the same check as for `kprintf`. `noreturn` tells GCC that
`panic` never returns. That lets GCC produce slightly better code after a
call to `panic`, and it stops GCC from warning about things like "this
function might not return a value" when a function ends with a `panic`.

### assert

An **assertion** is a check of something you believe is always true. If it
turns out to be false, there's a bug, and you want to know immediately,
with the location. Write it as a macro in `panic.h`:

```c
#define assert(e) \
    ((e) ? (void)0 : panic("assert(%s) failed at %s:%d", #e, __FILE__, __LINE__))
```

Three preprocessor features make this work:

- **`#e`** turns the macro argument into a string literal containing its
  source text. For `assert(1 + 1 == 3)`, `#e` is `"1 + 1 == 3"`.
- **`__FILE__`** and **`__LINE__`** are replaced by the current file name and
  line number, at the place where `assert` is used.
- The `?:` form (instead of an `if`) makes `assert(...)` an expression, so it
  works anywhere a function call would, including in places like
  `if (x) assert(y); else ...` where a macro containing an `if` would
  break.

So `assert(1 + 1 == 3);` on line 28 of `kmain.c` expands to:

```c
((1 + 1 == 3) ? (void)0
              : panic("assert(%s) failed at %s:%d", "1 + 1 == 3", "kernel/kmain.c", 28));
```

and prints:

```
PANIC: assert(1 + 1 == 3) failed at kernel/kmain.c:28
```

Use `assert` generously from now on. An assertion that fails turns "the
kernel behaves strangely three lessons later" into "a precise message,
right now, at the line that's wrong".

<details>
<summary>Reference: <code>lib/panic.c</code> and <code>lib/panic.h</code></summary>

```c
/* panic.c -- print a message and stop the machine. */
#include <stdarg.h>
#include "lib/panic.h"
#include "lib/kprintf.h"

void panic(const char *fmt, ...)
{
    va_list ap;

    __asm__ volatile ("cli");           /* nothing may interrupt us now */
    kprintf("\nPANIC: ");
    va_start(ap, fmt);
    kvprintf(fmt, ap);
    va_end(ap);
    kprintf("\n");

    for (;;)
        __asm__ volatile ("hlt");
}
```

```c
/* panic.h -- give up loudly. */
#ifndef LIB_PANIC_H
#define LIB_PANIC_H

void panic(const char *fmt, ...)
    __attribute__((noreturn, format(printf, 1, 2)));

#define assert(e) \
    ((e) ? (void)0 : panic("assert(%s) failed at %s:%d", #e, __FILE__, __LINE__))

#endif
```

`panic` prints a newline *before* `PANIC:` too, so the message starts on a
fresh line even if the kernel was in the middle of printing something.

</details>

---

## 10. Putting it together

`kmain` now becomes:

1. `console_init()`, first of all, so everything after it can print.
2. Print `toyos booting...`.
3. Check the Multiboot magic number, and `panic` if it's wrong. This is
   better than lesson 0's version, which returned silently. Note that
   `magic` is a `uint32_t`, so print it with `%lx`.
4. Print the two test lines from step 6.
5. `panic("nothing more to do (test %d)", 1)`. There's nothing else for the
   kernel to do yet, and this tests `panic`.

<details>
<summary>Reference: <code>kmain.c</code></summary>

```c
/* kmain.c -- the kernel's C entry point, called from boot.asm. */
#include <stdint.h>
#include "dev/console.h"
#include "lib/kprintf.h"
#include "lib/panic.h"

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002

void kmain(uint32_t magic, uint32_t mbi_phys)
{
    (void)mbi_phys;

    console_init();
    kprintf("toyos booting...\n");

    if (magic != MULTIBOOT_BOOTLOADER_MAGIC)
        panic("not booted by a Multiboot loader (magic=%lx)", magic);

    kprintf("kprintf test: %d %d %x %X \"%s\" '%c' 100%%\n",
            42, -17, 0xdeadbeef, 0xc0ffee, "str", 'x');
    kprintf("padding: [%08x] [%-5d] [%5s] [%d]\n", 0xbeef, 42, "ab", -2147483647 - 1);

    panic("nothing more to do (test %d)", 1);
}
```

</details>

Run `make run`. The screen is cleared, and both the screen and the terminal
show:

```
toyos booting...
kprintf test: 42 -17 deadbeef C0FFEE "str" 'x' 100%
padding: [0000beef] [42   ] [   ab] [-2147483648]

PANIC: nothing more to do (test 1)
```

---

## 11. Exercises

1. **Scrolling.** Before the final `panic`, print 30 numbered lines with
   `kprintf("line %d\n", i)`. The QEMU window should show `line 8` on the
   top row and `line 29` on row 21, then a blank line, the panic message
   on row 23, and the cursor blinking on the empty bottom row. The
   terminal shows all 30 lines. Can you explain why `line 8` ends up on
   top? (Count the rows everything takes.)

2. **Edge cases.** Check that each of these prints what's shown:

   ```c
   kprintf("[%x %d]", 0, 0);                  /* [0 0]           */
   kprintf("[%05d]", -42);                    /* [-0042]         */
   kprintf("[%p]", (void *)0xb8000);          /* [0x000b8000]    */
   kprintf("[%s]", (char *)0);                /* [(null)]        */
   ```

3. **Truncation.** Format into a buffer that's too small:

   ```c
   char small[8];
   ksnprintf(small, sizeof small, "%d-%d", 12345, 67890);
   kprintf("[%s]\n", small);                  /* [12345-6] */
   ```

   Why exactly 7 characters?

4. **Assertions.** Add `assert(1 + 1 == 3);` and check that the message
   names the right file and line.

5. **The format checker.** Write `kprintf("%d\n", "oops");` and read GCC's
   warning. Then remove the `format` attribute from `kprintf.h` and see
   that the warning disappears, and what gets printed instead. Put the
   attribute back.

6. **Find the cursor registers yourself.** Look up "CRT Controller
   Registers" in the OSDev wiki's "Text Mode Cursor" page. Find registers
   `0x0A` and `0x0B`, and use them to change the cursor's shape into a
   full block.

## Pitfalls

- **`%x` warnings for `uint32_t` values.** `uint32_t` is `unsigned long`
  with this compiler. Use `%lx` / `%lu`. Don't disable the warning.
- **`va_arg(ap, char)`.** A `char` passed through `...` arrives as an `int`.
  Read `int`, then convert.
- **Printing `-2147483648` as garbage.** You negated an `int`. Negate as
  `uint32_t`.
- **`0` prints as nothing.** The digit loop is a `while`; make it a
  `do ... while`.
- **Text on the screen but not in the terminal (or the other way
  round).** `console_putc` isn't calling both drivers, or `serial_init`
  wasn't called.
- **Staircase text in the terminal** (each line starts where the previous
  one ended): `serial_putc` isn't sending `'\r'` before `'\n'`.
- **Nothing on the screen.** The VGA pointer isn't `volatile`, or the color
  byte is `0x00` (black on black).
- **`undefined reference to memcpy` (or `memset`).** GCC generated a call
  for a structure copy or a large initializer. Make sure `lib/string.c` is
  being compiled.

## References

- OSDev wiki: "Serial Ports" (the 16550 registers), "Text Mode Cursor"
  (the CRT controller), "Printing To Screen".
- The 16550 UART datasheet (search for "PC16550D"), for every bit of every
  register.
- xv6, `console.c` and `uart.c`: another small kernel's console, with
  `cprintf`.
- The C standard's description of `<stdarg.h>`, or `man stdarg` on your Mac.
