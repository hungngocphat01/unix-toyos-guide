# 18 — The TTY

**Goal:** programs can **read from the terminal**. Keystrokes from the PS/2
keyboard *and* from the serial port go into a line editor (the *TTY*, or
"line discipline") with echo, Backspace, kill-line and ^D. Programs can
switch it to character-at-a-time mode with V7's `ioctl`/`stty`/`gtty`.

**Milestone** (`make run-nox`; your typing is echoed):

```
init: isatty(0) = 1, isatty of a file = 0

init: running cat: type lines, end with ^D
hello                     <- you typed this line, then Enter
hello                     <- cat's output
typo gone                 <- typed "worx<BS>ld <^U>typo gone<Enter>"
typo gone

init: running keys
keys: press keys, q to quit
  key 97 (0141)           <- a
  key 90 (0132)           <- Z
  key 27 (033)            <- Esc
init: done
```

**Reference code:** [`code/`](code/). New: [`dev/tty.c`](code/kernel/dev/tty.c),
[`dev/tty.h`](code/kernel/dev/tty.h), [`abi/sgtty.h`](code/abi/sgtty.h),
user [`libc/tty.c`](code/user/libc/tty.c) (`stty gtty isatty`),
[`<sgtty.h>`](code/user/libc/include/sgtty.h), [`keys.c`](code/user/bin/keys.c).
Changed: [`dev/kbd.c`](code/kernel/dev/kbd.c) and
[`dev/serial.c`](code/kernel/dev/serial.c) (input → `tty_input`),
[`dev/dev.c`](code/kernel/dev/dev.c) (`cdevsw` gains `ioctl`; console = TTY),
[`fs/file.c`](code/kernel/fs/file.c) (`file_ioctl`),
[`sys/sys_file.c`](code/kernel/sys/sys_file.c) (`sys_ioctl`, V7 number 54),
[`kmain.c`](code/kernel/kmain.c), [`init.c`](code/user/bin/init.c).

---

## 1. Why a line discipline?

Until now the keyboard IRQ printed characters directly. A program calling
`read(0, buf, 100)` needs something different:

- **Somewhere to put characters** typed before anyone reads them (a buffer
  filled by interrupt handlers, emptied by `read`).
- **Line editing**: you type `worx`, Backspace, `ld`. The program must
  receive `world\n`, never the mistake. Since every program wants this, the
  kernel does it once for all of them. That's *cooked* (canonical) mode:
  `read` returns only when a whole line is ready.
- **Echo**: the kernel (not the program) prints what you type.
- **Special keys**: ^D = end of file, ^U = erase the line, and later
  ^C/^\\ = signals (lesson 26).
- **Escape hatches** for programs like editors that want every key
  immediately and no echo: *CBREAK* (keys at once, keep ^C) and *RAW*
  (no processing at all).

```
 keyboard IRQ ─► kbd.c: scancode → ASCII ─┐
                                          ├─► tty_input(c) ──► input[] ──► tty_read ◄── read(0, ...)
 serial IRQ ──► serial.c: byte ───────────┘        │
                                                   └─ echo ─► console_putc ─► VGA + serial
 write(1, ...) ──► tty_write ──────────────────────────────► console_putc
```

The serial port is an input too: what you type in the terminal running
QEMU reaches the TTY exactly like keyboard input. That's how real Unix
machines were used (a terminal on a serial line), and it lets you script
tests: `printf 'ls\r' | make run-test`.

## 2. The input buffer: three indices

One circular buffer, three free-running counters (they only grow; index
the array with `% TTY_BUF`):

```
 input[]:  [ h e l l o \n | w o r ]
             ^r            ^w     ^e
 r..w   complete lines: read() takes from here
 w..e   the line being typed: Backspace and ^U only touch this part
```

- `tty_input(c)` appends at `e`. Erase does `e--` (if `e > w`), and echoes
  `"\b \b"` (back, overwrite with a space, back). Kill erases back to `w`.
- When Enter (or ^D) arrives, `w = e`: the line is committed and `read` can
  have it. In CBREAK/RAW mode every character commits immediately.
- `tty_read` copies from `r` while `r != w`, stopping after `\n` (one line
  per `read`, as in V7). A ^D is not copied: it just ends the `read`. ^D at
  the start of a line makes `read` return 0, which programs treat as end
  of file. That's all EOF on a terminal is!

```c
void tty_input(char c)
{
    ...
    if (c == tty.mode.sg_erase) {
        if (tty.e != tty.w) {
            tty.e--;
            echo('\b'); echo(' '); echo('\b');
        }
        return;
    }
    ...
    tty.input[tty.e++ % TTY_BUF] = c;
    if (c != tty.chars.t_eofc)
        echo(c);
    if ((flags & CBREAK) || c == '\n' || c == tty.chars.t_eofc || buffer full)
        tty.w = tty.e;
}
```

(This is xv6's `console.c` design. V7 used two queues, `rawq` and `canq`;
same idea.)

## 3. Waiting for input (without a scheduler)

`read` arrives with interrupts **off** (`int 0x80` is an interrupt gate).
If no line is ready, we must wait for the keyboard, so we turn them on
and halt:

```c
static void wait_for_input(void)
{
    while (tty.r == tty.w)
        __asm__ volatile ("sti; hlt; cli");
}
```

The CPU sleeps until the next interrupt. The keyboard IRQ handler runs on
the same kernel stack, *on top of* the waiting `read`, calls
`tty_input`, and returns. Then the loop checks again.

Why `sti; hlt` in one asm statement? If an interrupt arrived between a
separate `sti()` and `hlt()`, its key would be handled, and then `hlt`
would sleep without anyone checking the buffer (until the next timer tick,
10 ms later; with no timer, forever). x86 guarantees that `sti` only takes
effect after the *next* instruction, so `sti; hlt` has no gap.

This works only because one process runs at a time. With several
processes (lesson 24) a waiting `read` must let *other* processes run:
this loop becomes `sleep(&tty)` and `tty_input` calls `wakeup(&tty)`.

## 4. ioctl: terminal modes, the V7 way

V7 has no `stty`/`gtty` system calls (older Unixes did). Its libc builds
them on `ioctl(fd, request, arg)`, system call 54, the "everything else"
call for devices:

```c
int stty(int fd, struct sgttyb *mode) { return ioctl(fd, TIOCSETP, mode); }
int gtty(int fd, struct sgttyb *mode) { return ioctl(fd, TIOCGETP, mode); }
int isatty(int fd) { struct sgttyb m; return gtty(fd, &m) == 0; }
```

(`isatty` is literally "can I get terminal modes?". `stdio` uses it in
lesson 20 to decide whether to buffer stdout by line or by block.)

The kernel route: `sys_ioctl` → `file_ioctl` (only for character devices,
else `ENOTTY`) → `cdevsw[major].ioctl` → `tty_ioctl`. Since only the
driver knows how big `arg` is, the driver checks it with `user_ok`.

The structures and request numbers are V7's own ([`abi/sgtty.h`](code/abi/sgtty.h)),
so V7 programs (`stty`, `more`, `vi`-like editors, games) compile
unchanged:

```c
struct sgttyb { char sg_ispeed, sg_ospeed; char sg_erase, sg_kill; int sg_flags; };
#define CBREAK 0002
#define ECHO   0010
#define CRMOD  0020             /* input: CR -> NL (output NL -> CR NL: always) */
#define RAW    0040
#define TIOCGETP (('t'<<8)|8)
#define TIOCSETP (('t'<<8)|9)  /* also discards unread input */
#define TIOCSETN (('t'<<8)|10)
```

**Default keys.** V7 used `#` to erase and `@` to kill a line: on a
printing terminal you can't un-print. We use Backspace (`\b`; DEL from a
serial terminal is converted), ^U, ^C, ^\\ and ^D. A program can change
them with `TIOCSETP`/`TIOCSETC`.

**A program that changes modes must restore them** before exiting, or
your shell is left with no echo. ([`keys.c`](code/user/bin/keys.c) does.
Lesson 26: also when killed by a signal.)

## 5. Notes on QEMU

- `make run`: type in the QEMU window (PS/2 keyboard) *or* in the
  terminal (serial). ^C in the terminal kills QEMU itself, not your program.
- `make run-nox`: the terminal is the only console; ^C goes to toyos.
  Quit QEMU with Ctrl-A then X.
- Terminals send `\r` for Enter (CRMOD turns it into `\n`) and DEL (0177)
  for Backspace.

---

## 6. Modules

| File | Responsibility | Exposes |
|---|---|---|
| [`dev/tty.c`](code/kernel/dev/tty.c) | line discipline, modes | `tty_init tty_input tty_read tty_write tty_ioctl` |
| [`dev/kbd.c`](code/kernel/dev/kbd.c) | scancodes → `tty_input` | |
| [`dev/serial.c`](code/kernel/dev/serial.c) | receive interrupt (IRQ 4) → `tty_input` | `serial_enable_input` |
| [`dev/dev.h`](code/kernel/dev/dev.h) | `cdevsw` gains `ioctl` | |
| [`fs/file.c`](code/kernel/fs/file.c) | `file_ioctl` | |
| [`abi/sgtty.h`](code/abi/sgtty.h) | V7 `sgttyb`, `tchars`, `TIOC*` | |
| [`user/libc/tty.c`](code/user/libc/tty.c) | `stty gtty isatty` | |

## 7. Test

- The milestone, typing yourself, and scripted:

  ```sh
  (sleep 2; printf 'hello\r'; sleep .3; printf 'worx\177ld\r\004'; sleep .5; printf 'aq') \
      | timeout 6 make run-test
  ```

  The `sleep`s matter: if all input arrives at once, the echo of what
  you type (handled at interrupt time) interleaves with program output.
- Type more than 256 characters on one line: the extra ones are dropped,
  and Enter still works.
- `read(0, buf, 3)` on the line `hello\n` returns `hel`, then `lo\n`.
- `ioctl` on `/etc/motd` → `ENOTTY`.

## Pitfalls

- **Losing a key between `sti` and `hlt`**: see §3.
- **Erasing past the start of the line** (into text already committed, or
  the prompt): check `e != w`.
- **Forgetting to read the UART data register** in the IRQ: the UART
  stops interrupting. Read *all* waiting bytes (`while (LSR & 1)`): the
  FIFO can hold several.
- **Serial IRQ never fires**: you need `IER = 1` *and* `OUT2` in the modem
  control register (set since lesson 1) *and* the PIC line unmasked.
- **Echo of ^D**: don't. And don't echo when the program turned ECHO off
  (passwords!).
- **`TIOCSETP` with a half-typed line**: V7 throws it away (`TIOCSETP`) or
  keeps it (`TIOCSETN`). When switching to CBREAK, commit it (`w = e`) so
  the program sees it.

## References

- V7: `usr/sys/dev/tty.c` (`ttyinput`, `canon`, `ttread`, `ttioctl`),
  `usr/sys/h/tty.h`, `usr/include/sgtty.h`, `usr/src/libc/gen/stty.c`,
  `isatty.c`, `usr/src/cmd/stty.c`.
- Lions' Commentary, chapters 23–25 (the V6 TTY, very similar).
- xv6: `console.c` (`consoleintr`, the r/w/e buffer).
- OSDev wiki: "Serial Ports" (interrupts, LSR, IER).
- "The TTY demystified" (Linus Åkesson): <https://www.linusakesson.net/programming/tty/>.
