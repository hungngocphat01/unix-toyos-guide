# 5 — Hardware Interrupts

**Goal:** devices interrupt the CPU: a timer ticking 100 times a second
and a keyboard whose keys appear on screen.

**Milestone:** type in the QEMU window; characters appear as you type.
Every 5 seconds:

```
interrupts enabled; type something
hi!
[uptime 5s]
[uptime 10s]
```

**Reference code:** [`code/`](code/). New: [`pic.c`](code/kernel/arch/i386/pic.c),
[`pit.c`](code/kernel/arch/i386/pit.c), [`kbd.c`](code/kernel/dev/kbd.c).
Changed: [`trap.c`](code/kernel/arch/i386/trap.c) (IRQ dispatch),
[`trap.h`](code/kernel/arch/i386/trap.h), [`kmain.c`](code/kernel/kmain.c).

---

## Concepts

### The path of a key press

Nothing new happens on the CPU side: an IRQ is just another vector going
through the path you built in lesson 4. The new part is the chip in between:

```
 key pressed
     │
 keyboard controller ──IRQ line 1──► 8259 PIC (master) ──INTR pin──► CPU
                                      "IRQ 1 = vector 33"              │
                                                                       │ if IF=1, after the
                                                                       ▼ current instruction
                             IDT[33] → stub33 → trap_common → trap(tf)
                                                                       │
                             trap(): irq = 33 - 32 = 1
                                     pic_eoi(1)          ← "I got it, you may send more"
                                     irq_handlers[1]()   ← kbd_irq(): inb(0x60), translate, print
```

### The PIC (8259), and why it must be remapped

Two chips, cascaded: the **master** handles IRQ 0–7, the **slave** IRQ 8–15
and is wired into the master's IRQ 2.

| IRQ | Device |
|---|---|
| 0 | PIT timer |
| 1 | keyboard |
| 2 | (the slave PIC) |
| 4 | COM1 |
| 14 | primary ATA disk |

The BIOS leaves the master sending IRQ 0–7 as **vectors 8–15**. Those are CPU
exceptions: IRQ 0, the timer, would arrive as vector 8, **double fault**.
So the first thing to do is reprogram both chips to use vectors 32–47.
That takes four "initialization command words" per chip; the sequence is
fixed and you can copy it from the datasheet or OSDev:

```c
outb(MASTER_CMD, 0x11);  outb(SLAVE_CMD, 0x11);   /* ICW1: init, expect ICW4   */
outb(MASTER_DATA, 32);   outb(SLAVE_DATA, 40);    /* ICW2: vector base         */
outb(MASTER_DATA, 4);    outb(SLAVE_DATA, 2);     /* ICW3: slave on IRQ2       */
outb(MASTER_DATA, 0x01); outb(SLAVE_DATA, 0x01);  /* ICW4: 8086 mode           */
```

Then the **mask** (port `0x21`/`0xA1`, 1 bit per IRQ, 1 = disabled). Start
with everything masked except the cascade line, and let each driver unmask
its own IRQ when it's ready (`pic_enable(irq)`).

### End Of Interrupt

After raising an IRQ, the PIC waits for an **EOI** (`outb(0x20, 0x20)`, and
also to `0xA0` for IRQ ≥ 8) before sending another of equal or lower
priority. **Forget it and you get exactly one tick and one key, then
silence.** That's the most common bug of this lesson.

We send EOI **before** calling the handler. Interrupts are still disabled
(we're inside an interrupt gate), so nothing can arrive early. And from
lesson 24, the timer handler may switch to another process and not
"return" for a long time; EOI-first keeps the timer running.

### Dispatch: a table of handlers

`trap()` shouldn't know about keyboards. Drivers register a function for
their IRQ instead ([`trap.c`](code/kernel/arch/i386/trap.c)):

```c
static void (*irq_handlers[NIRQ])(void);

void trap(struct trapframe *tf)
{
    if (tf->trapno >= T_IRQ0 && tf->trapno < T_IRQ0 + NIRQ) {
        int irq = tf->trapno - T_IRQ0;
        pic_eoi(irq);
        if (irq_handlers[irq])
            irq_handlers[irq]();
        return;
    }
    ...exceptions as before...
}
```

Each driver's `init` does two things: `irq_register(IRQ_X, handler)`, then
`pic_enable(IRQ_X)`.

### The PIT (timer)

A chip with a 1.193182 MHz input clock and a 16-bit divider. Program
channel 0 with divisor `1193182 / HZ` and it raises IRQ 0 `HZ` times per
second (`HZ = 100` here; V7 used 60). The handler just counts `ticks`.
That counter is the kernel's sense of time, and in lesson 24 its interrupt
is what takes the CPU away from a running process.

`ticks` is `volatile`: it changes "behind the compiler's back" (in an
interrupt), and without `volatile` the idle loop could read it once and
cache it in a register forever.

### The keyboard

On IRQ 1, read one byte from port `0x60`: a **scancode** (set 1). Codes
`0x01..0x58` are key *presses*; the same code + `0x80` is the *release*.
Shift/Ctrl/Caps Lock are keys too: track their state from press/release,
then look up the character in a table (one table for normal, one for
shifted). Ctrl+letter = letter − 0x60 (Ctrl-C = 3, Ctrl-D = 4: the TTY will
care in lesson 18).

You **must** read port `0x60` on every keyboard IRQ, even for keys you
ignore; otherwise the controller never raises another one.

### `sti` and `hlt`

- `sti` sets IF=1: from now on IRQs are delivered. Do it **after** the
  IDT, PIC and every handler are ready.
- `hlt` stops the CPU until the next interrupt. The idle loop is `hlt`,
  check if anything changed, `hlt`... It's also why QEMU doesn't use 100%
  of a host core.

---

## Modules

| File | Responsibility | Exposes |
|---|---|---|
| [`arch/i386/pic.c`](code/kernel/arch/i386/pic.c) | remap, mask, EOI | `pic_init pic_enable(irq) pic_eoi(irq)`, `IRQ_*` numbers |
| [`arch/i386/pit.c`](code/kernel/arch/i386/pit.c) | timer at `HZ` | `pit_init pit_ticks`, `HZ` |
| [`dev/kbd.c`](code/kernel/dev/kbd.c) | scancode → ASCII, echo | `kbd_init` |
| [`arch/i386/trap.c`](code/kernel/arch/i386/trap.c) | IRQ dispatch table | `irq_register(irq, fn)` |
| [`kmain.c`](code/kernel/kmain.c) | `pic_init pit_init kbd_init`, `sti`, idle loop | |

Where the keyboard's characters go is the one thing that will change: here
`kbd_irq` calls `console_putc`. In lesson 18 it calls `tty_input()`, and the
TTY decides about echo, line editing and who gets the characters.

## Test

- The milestone. Then hold a key down: auto-repeat should work.
- Comment out `pic_eoi`: one key, then nothing. Put it back.
- Measure: print `pit_ticks()` after a 10-second stopwatch. Should be ~1000.
- Scripted test (no window needed), using the QEMU monitor:

  ```sh
  make run-test QEMUEXTRA="-monitor tcp:127.0.0.1:4444,server,nowait" &
  echo "sendkey h" | nc -w1 127.0.0.1 4444
  ```

## Pitfalls

- **One tick, then nothing**: missing EOI.
- **Double fault right after `sti`**: PIC not remapped, so the timer
  arrived as vector 8.
- **Keyboard works once**: not reading port `0x60`.
- **Garbage key = vector 39 (IRQ 7) at random**: a *spurious* interrupt the
  8259 can generate. Harmless here (no handler registered); a robust kernel
  checks the PIC's in-service register and skips the EOI for spurious IRQs.
- **Idle loop never notices ticks**: `ticks` isn't `volatile`.

## References

- OSDev: "8259 PIC", "PIT", "PS/2 Keyboard", "Keyboard scancode set 1"
- Intel 8259A datasheet (ICW/OCW descriptions)
- xv6: `picirq.c`, `kbd.c`, `kbd.h`, `timer` handling in `trap.c`
