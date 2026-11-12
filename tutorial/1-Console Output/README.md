# 1 — Console Output

**Goal:** a real text console and a `kprintf` you'll use to debug everything
that follows.

**Milestone** (`make run`): the screen is cleared and this appears on both
VGA and serial:

```
toyos booting...
kprintf test: 42 -17 deadbeef C0FFEE "str" 'x' 100%
padding: [0000beef] [42   ] [   ab] [-2147483648]

PANIC: nothing more to do (test 1)
```

**Reference code:** [`code/`](code/) is the whole codebase at the end of this
lesson. To see exactly what changed since the last lesson:

```sh
cd tutorial
diff -ru "0-Environment and Hello World/code" "1-Console Output/code"
```

---

## Concepts

### Two output devices, one console

You want output on **both**:

- **VGA text mode** is what a user sees, but it has 25 lines, no scrollback,
  and it's gone when QEMU dies.
- **Serial (COM1)** goes to your terminal (`-serial stdio`): scrollback,
  copy-paste, `make run-nox | tee log.txt`. When the kernel is broken,
  serial is what saves you.

So split responsibilities into layers. Each layer knows only the one below:

```
kprintf("x=%d", x)        lib/kprintf.c   formatting only: turns args into chars
      │ one char at a time
      ▼
console_putc(c)           dev/console.c   "the console": fans out to all devices
      │            │
      ▼            ▼
 vga_putc(c)   serial_putc(c)             dev/vga.c, dev/serial.c: one device each
```

`kprintf` doesn't know VGA exists; `vga.c` doesn't know what `%d` is. In
lesson 18 the TTY driver will sit on top of `console_putc` too.

### VGA text mode

- Buffer at `0xB8000`: 80 × 25 cells of 16 bits: `char | (color << 8)`.
  Cell index = `row * 80 + col`.
- **Scrolling** = copy rows 1..24 up one row, blank row 24.
- **The blinking cursor** doesn't follow your writes. You move it through the
  CRT controller (ports `0x3D4`/`0x3D5`, registers `0x0E`/`0x0F` = high/low
  byte of the cell index).
- Control characters: `\n`, `\r`, `\b` (the TTY's erase relies on it), `\t`.

The whole driver is ~80 lines: [`code/kernel/dev/vga.c`](code/kernel/dev/vga.c).
The interesting part:

```c
void vga_putc(char ch)
{
    switch (ch) {
    case '\n': row++; col = 0;              break;
    case '\r': col = 0;                     break;
    case '\b': if (col > 0) col--;          break;
    case '\t': col = (col + 8) & ~7;        break;
    default:   put_cell(row, col, ch); col++; break;
    }
    if (col >= COLS) { col = 0; row++; }
    if (row >= ROWS) scroll();
    move_cursor();
}
```

### Serial port (16550 UART)

The barebone wrote to COM1 without setting it up. QEMU tolerates that; real
hardware doesn't, and you'll want serial *input* later. Setup, all relative
to `0x3F8` ([`code/kernel/dev/serial.c`](code/kernel/dev/serial.c)):

| Offset | Write | Meaning |
|---|---|---|
| +1 | `0x00` | no UART interrupts (for now) |
| +3 | `0x80` | DLAB=1: offsets +0/+1 now mean "baud divisor" |
| +0, +1 | `3`, `0` | divisor 3 → 115200/3 = 38400 baud |
| +3 | `0x03` | 8 bits, no parity, 1 stop bit, DLAB=0 |
| +2 | `0xC7` | enable and clear FIFOs |
| +4 | `0x0B` | DTR, RTS, OUT2 (OUT2 must be set for IRQs to reach the PIC) |

Sending a byte = spin until bit 5 of the Line Status Register (+5) says the
transmitter is empty, then `outb` to +0. Terminals want `\r\n`, so
`serial_putc('\n')` sends `\r` first.

### `kprintf`: a formatter with a pluggable output

The core takes an **output callback**, so the same code serves `kprintf`
(callback = `console_putc`) and `ksnprintf` (callback = store in a buffer).
You'll want `ksnprintf` when building strings like process names.

```c
typedef void (*putc_fn)(char c, void *ctx);
static void format(putc_fn out, void *ctx, const char *fmt, va_list ap);
```

The format loop: copy plain characters; on `%`, parse flags (`-`, `0`),
width, an ignored `l`, then the conversion. Numbers are converted **least
significant digit first, into the end of a small buffer**, which avoids a
reversal step:

```c
do {
    buf[--i] = digits[n % base];
    n /= base;
} while (n);              /* do-while: 0 still prints "0" */
```

Signed numbers: print `-` and the magnitude. Compute the magnitude as
`-(uint32_t)v`, because `-v` overflows for `INT_MIN`.

Full code: [`code/kernel/lib/kprintf.c`](code/kernel/lib/kprintf.c).

### `panic` and `assert`

`panic` prints `PANIC: <message>`, disables interrupts and halts forever.
`assert(e)` panics with the expression text, file and line:

```c
#define assert(e) \
    ((e) ? (void)0 : panic("assert(%s) failed at %s:%d", #e, __FILE__, __LINE__))
```

Use asserts generously from now on. They turn "weird behavior three
lessons later" into "a precise message right now".

---

## Modules

| File | Responsibility | Exposes |
|---|---|---|
| [`arch/i386/io.h`](code/kernel/arch/i386/io.h) | port I/O, moved out of `kmain.c` | `inb outb inw outw io_wait` (all `static inline`) |
| [`lib/string.c`](code/kernel/lib/string.c) | `<string.h>` subset | `memset memcpy memmove memcmp strlen strcmp strncmp strncpy` |
| [`dev/serial.c`](code/kernel/dev/serial.c) | COM1 | `serial_init serial_putc` |
| [`dev/vga.c`](code/kernel/dev/vga.c) | text screen, cursor, scrolling | `vga_init vga_putc` |
| [`dev/console.c`](code/kernel/dev/console.c) | fan-out to all output devices | `console_init console_putc` |
| [`lib/kprintf.c`](code/kernel/lib/kprintf.c) | formatting | `kprintf kvprintf ksnprintf` |
| [`lib/panic.c`](code/kernel/lib/panic.c) | stop the machine | `panic`, `assert` macro |
| [`kmain.c`](code/kernel/kmain.c) | boot sequence | calls `console_init`, prints tests |

Two build changes in the [`Makefile`](code/Makefile):

- `-Ikernel`: includes are written relative to `kernel/`, e.g.
  `#include "dev/vga.h"`, so you can always tell which directory a header
  is in.
- `-fno-tree-loop-distribute-patterns`: see the pitfall about `memset` below.

## Steps

1. `io.h`, then `string.c`. Build (unused, but it must compile).
2. `serial.c`, called from `kmain`. `make run-nox` → text in the terminal.
3. `vga.c`: clear + putc without scrolling, then add scrolling. Test by printing
   40 numbered lines.
4. Hardware cursor.
5. `console.c`.
6. `kprintf`: `%s %c %%`, then `%x`, then `%d %u`, then width/flags.
7. `panic`, `assert`.

## Test

- The milestone output above. Pay attention to `-2147483648` and to zero
  (`kprintf("%d %x", 0, 0)` must print `0 0`).
- Print 100 lines: the screen scrolls, the cursor sits after the last char.
- Make `kmain` call `assert(1 + 1 == 3)` and check the message.

## Pitfalls

- **`uint32_t` is `unsigned long` with `i686-elf-gcc`**, not `unsigned int`.
  `kprintf("%x", some_uint32)` gets a `-Wformat` warning: use `%lx`/`%lu`
  (our `kprintf` skips the `l`). Keep `-Wall`; this warning catches real bugs.
- **`memset` becomes infinite recursion.** At `-O2`, GCC recognizes your
  byte loop *as* memset and replaces it with a call to `memset`, which is
  itself. `-fno-tree-loop-distribute-patterns` prevents that.
- **`va_arg(ap, char)`** is wrong: `char`/`short` are promoted to `int`
  through `...`.
- **Nothing on VGA**: missing `volatile`, or color byte `0x00` (black on black).

## References

- OSDev: "Text Mode Cursor", "Printing To Screen", "Serial Ports"
- xv6 `console.c` (`cprintf`, `cgaputc`, `uartputc`)
