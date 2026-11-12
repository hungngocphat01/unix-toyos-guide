# Lesson 4 — Interrupts and Exceptions

Until now, if the CPU hit an error, such as a division by zero, the machine
simply reset, with no message. In this lesson you teach the CPU what to do
instead: stop what it's doing, save its state, and call a function in our
kernel that can print what happened.

When you're done, the kernel survives a breakpoint and reports a division
by zero like this:

```
toyos booting...
GDT and IDT loaded
breakpoint at eip=00100f2b, continuing
back in kmain after int3
dividing by zero...
trap 0 (divide error), err=0
  eip=00100f4c cs=0008 eflags=00010046
  eax=00001234 ebx=00009500 ecx=00000000 edx=00000000
  esi=00000000 edi=00001000 ebp=00107880
  ds=0010 es=0010 fs=0010 gs=0010

PANIC: unhandled trap 0
```

This is one of the most important lessons in the tutorial. The mechanism
you build here is the single path through which **every** interrupt will
reach the kernel: errors in this lesson, the clock and the keyboard in
lesson 5, page faults in lesson 7, system calls in lesson 11, and the switch
between programs in lesson 24. Each of those lessons will only add a few
lines to what you write now. So take your time: once you really understand
this lesson, interrupts stop being mysterious.

The reference code is in [`code/`](code/). To see every change since
lesson 3:

```sh
cd tutorial
diff -ru "3-The GDT/code" "4-Interrupts and Exceptions/code"
```

**Contents**

1. [What an interrupt is](#1-what-an-interrupt-is)
2. [What the CPU does when an interrupt happens](#2-what-the-cpu-does-when-an-interrupt-happens)
3. [The IDT: where the CPU finds the handler](#3-the-idt-where-the-cpu-finds-the-handler)
4. [Why the handler's first instructions must be in assembly](#4-why-the-handlers-first-instructions-must-be-in-assembly)
5. [The design: 256 stubs, one common path, one C function](#5-the-design-256-stubs-one-common-path-one-c-function)
6. [The trap frame: the saved state, as a C structure](#6-the-trap-frame-the-saved-state-as-a-c-structure)
7. [The C side: trap()](#7-the-c-side-trap)
8. [Writing it](#8-writing-it)
9. [Exercises](#9-exercises)

---

## 1. What an interrupt is

### The problem

A CPU executes instructions one after the other: it fetches an
instruction, executes it, and moves on to the next. But sometimes something
needs attention *right now*, and the program that's running has no idea
it's coming. Three kinds of event cause this:

1. **The current instruction can't be executed.** It divides by zero, or
   accesses memory it isn't allowed to, or isn't a valid instruction. The
   CPU can't just skip it and carry on as if nothing happened. These events
   are called **exceptions**.
2. **A device needs attention.** A key was pressed, the clock ticked, the
   disk finished reading. The device doesn't know or care what the CPU is
   doing at that moment. These are **hardware interrupts** (lesson 5).
3. **A program asks the kernel to do something.** A user program can't
   talk to devices itself (lesson 3 explained that ring 3 isn't allowed
   to), so it needs a way to call the kernel. The `int` instruction does
   this on purpose. These are **software interrupts**, and they're how
   **system calls** work (lesson 11).

All three are handled by the same CPU mechanism. To keep things simple,
"interrupt" in this lesson means any of the three.

### An interrupt is a function call that the CPU makes by itself

The easiest way to think about an interrupt: **it's a function call that
the CPU inserts between two instructions of whatever is running.** The CPU
stops the current program, calls a function in the kernel called the
**interrupt handler**, and when the handler returns, the program continues
exactly where it was, as if nothing had happened. The interrupted program
never asked for this call and usually doesn't know it happened.

It differs from a normal `call` in a few ways, and each difference shapes
the code we'll write:

| | A normal `call` | An interrupt |
|---|---|---|
| **When** | When the program decides | When the CPU decides: between any two instructions |
| **Which function** | The address is in the instruction | The CPU looks it up in a table, by a number called the **vector** |
| **What's pushed on the stack** | The return address | `EFLAGS`, `CS` and `EIP`, and sometimes an error code |
| **How to return** | `ret` | `iret` ("interrupt return") |
| **Registers** | The caller expects some registers to change | The interrupted code expects **nothing** to change |

The last row is the important one. When C code calls a function, both
sides follow the calling convention: the caller knows that `EAX`, `ECX`
and `EDX` may be changed by the call. An interrupted program made no call
and has no such expectation: it could be in the middle of computing
something in `ECX`. **The handler must leave every register exactly as it
found it.**

### Vector numbers

Every kind of interrupt has a number from 0 to 255, called its **vector**.
The CPU uses it to find the handler. Intel fixed the meaning of vectors 0
to 31, which are the exceptions. Here are the ones you'll meet:

| Vector | Name | Caused by |
|---|---|---|
| 0 | divide error | `div` or `idiv` by zero, or a result too large to fit |
| 3 | breakpoint | the `int3` instruction, which debuggers use |
| 6 | invalid opcode | bytes that aren't a valid instruction |
| 8 | double fault | an exception while the CPU was trying to start the handler of another one |
| 13 | general protection | a forbidden operation, such as a bad segment (lesson 3's exercises) or a ring 3 program executing `hlt` |
| 14 | page fault | an access to memory that isn't mapped (lesson 7) |

Vectors 32 to 255 are free for the operating system. We'll use 32 to 47
for hardware interrupts (lesson 5), and 128 (`0x80`) for system calls
(lesson 11).

### What happens today, without handlers

You've already seen it, in lesson 0's divide-by-zero exercise and lesson
3's broken GDTs. When an exception happens and the CPU can't find a
handler, it raises a second exception, the **double fault** (vector 8), to
report "I failed to handle an exception". If it can't find a handler for
that either, there's nothing left to try: the CPU gives up and resets the
whole machine. That's a **triple fault**. In this lesson we give the CPU a
handler for every vector, so this stops happening.

---

## 2. What the CPU does when an interrupt happens

### Step by step

Let's follow what happens when the CPU executes `div ecx` with `ECX` = 0.
These are real addresses from the reference kernel. The `div` is at address
`0x100f4c`, and `ESP` is `0x107868` just before it.

1. **The CPU detects the problem** while executing `div` and decides to
   raise exception 0.
2. **It finds the handler** for vector 0 in a table called the **IDT**
   (section 3).
3. **It pushes three values on the current stack**: `EFLAGS`, then `CS`,
   then `EIP`. For some exceptions, it also pushes an **error code** with
   more details about the problem (see below). Vector 0 has none.
4. **It turns interrupts off**, by clearing the `IF` flag in `EFLAGS`, so
   that a second interrupt can't arrive while the handler is still saving
   registers. (This depends on the kind of IDT entry; section 3.)
5. **It jumps to the handler**, loading `CS` and `EIP` from the IDT entry.

Here's the stack at the moment the handler's first instruction runs:

```
address     value         pushed by
0x107868                  (ESP before the interrupt)
0x107864    0x00010046    CPU: EFLAGS
0x107860    0x00000008    CPU: CS     (the kernel's code selector)
0x10785c    0x00100f4c    CPU: EIP    (the address of the div)   ◄── ESP now
```

`EFLAGS` and `CS` are saved because the handler might run with a different
`CS` and different flags (for example, interrupts off), and both must be
restored when going back.

At the end of the handler, the instruction **`iret`** does the reverse: it
pops `EIP`, `CS` and `EFLAGS` from the stack, all at once, and the
interrupted program continues. Notice that `iret` doesn't know about the
error code: if there is one, the handler must remove it from the stack
before `iret`.

(When an interrupt arrives while a **user** program is running, the CPU
also switches to a different stack and pushes two more values. That's for
lesson 10. In this lesson, all interrupts happen while the kernel is
running.)

### Faults and traps: which EIP is saved?

Look at the saved `EIP` again: it's `0x100f4c`, the address of the `div`
*itself*, not of the next instruction. That's deliberate, and it depends on
the kind of exception:

- A **fault** saves the address of the instruction that failed. If the
  handler returns, **the CPU runs that instruction again.** This is useful
  when the handler can fix the problem: in a later lesson, a page fault
  handler could make the missing memory available and let the program
  retry, and it would never know anything happened. But if the handler
  can't fix the problem, returning means failing again, forever. Divide
  error, general protection and page fault are faults.
- A **trap** saves the address of the *next* instruction. Returning
  continues after it. The breakpoint (`int3`) and the system call
  instruction (`int 0x80`) are traps: they did exactly what they were
  supposed to, and the program should carry on.

In the milestone output, the breakpoint line says `eip=00100f2b`. The
`int3` instruction is at `0x100f2a` and is one byte long, so the saved
`EIP` points to the instruction after it. That's why our kernel can simply
return from the breakpoint handler and continue.

### Error codes

For a few exceptions, the CPU pushes an extra 4-byte value after `EIP`,
the **error code**, which gives more detail. For example, for a general
protection fault caused by a bad segment, it's the selector that caused it
(you saw `e=0010` in lesson 3's QEMU logs). For a page fault, it says
whether the access was a read or a write, from ring 0 or ring 3.

The exceptions that push an error code are **8, 10, 11, 12, 13, 14, 17, 21,
29 and 30**. All the others, and all hardware and software interrupts,
don't. (This list comes from the Intel manual, Volume 3A, Table 6-1.) Our
handler will have to deal with both cases, which is the subject of
section 5.

---

## 3. The IDT: where the CPU finds the handler

### The table

The **Interrupt Descriptor Table**, or **IDT**, is an array of 256 entries,
one per vector. Each entry is 8 bytes and is called a **gate**. A gate says
"for this vector, run the code at this address, in this code segment". It
also says who's allowed to trigger it.

Like the GDT, its fields are cut into pieces for historical reasons:

```
byte:    0      1      2      3      4      5        6      7
      ┌──────┬──────┬──────┬──────┬──────┬────────┬──────┬──────┐
      │  offset     │  selector   │  0   │  type  │  offset     │
      │  0..15      │             │      │  attr  │  16..31     │
      └──────┴──────┴──────┴──────┴──────┴────────┴──────┴──────┘
```

- **Offset**: the address of the handler, split into two 16-bit halves.
- **Selector**: the code segment to run the handler in. We use `0x08`,
  the kernel code segment from lesson 3. That's what makes the handler run
  in ring 0, even when the interrupted code was in ring 3. It's also one of
  the reasons we needed our own GDT: the IDT refers to it.
- **Type and attributes** (byte 5), which is made of:

```
  bit:   7     6  5     4     3  2  1  0
       ┌───┬────────┬─────┬──────────────┐
       │ P │  DPL   │  0  │     type     │
       └───┴────────┴─────┴──────────────┘
```

- **P** ("present"): 1 means this gate is valid.
- **DPL**: the lowest privilege allowed to trigger this vector *with an
  `int` instruction*. With DPL 0, a ring 3 program that executes `int 3`
  gets a general protection fault instead. Hardware interrupts and
  exceptions ignore the DPL; it's only about the `int` instruction. All our
  gates use DPL 0 for now. In lesson 11, vector `0x80` becomes DPL 3, so
  that user programs can make system calls.
- **Type**: `0xE` is a 32-bit **interrupt gate**, which turns interrupts
  off when entering the handler (step 4 in section 2). `0xF` would be a
  **trap gate**, which leaves them as they were. We use interrupt gates
  everywhere: the handler is simpler to write if nothing can interrupt it.

So for our gates, byte 5 is `1 00 0 1110` in binary, which is **`0x8E`**.

### A real gate

Here's IDT entry 0 in the reference kernel, read with GDB:

```
(gdb) x/xg &idt
0x103080 <idt>:  0x00108e0000080f80

byte:     0    1    2    3    4    5    6    7
value:   80   0f   08   00   00   8e   10   00

offset bits 0..15   = 0x0f80
selector            = 0x0008     kernel code
zero                = 0x00
type and attributes = 0x8e       present, DPL 0, interrupt gate
offset bits 16..31  = 0x0010

handler address = 0x0010_0f80
```

And `0x100f80` is the address of `stub0`, the handler for vector 0
(`i686-elf-nm build/kernel.elf | grep stub0`).

### Loading it

The CPU finds the IDT through a register called **IDTR**, loaded with the
instruction **`lidt`**. It takes exactly the same 6-byte structure as `lgdt`
in lesson 3: the size of the table minus 1, then its address. For 256
entries of 8 bytes, the limit is `256 × 8 - 1 = 2047` (`0x7FF`):

```
(gdb) x/6xb &idtr
0x103060 <idtr>:  0xff  0x07  0x80  0x30  0x10  0x00
                  limit 0x07ff    base 0x00103080
```

Unlike the GDT, there's nothing to reload afterwards: the CPU reads the
IDT each time an interrupt happens. And, again, the table must be a global
variable, never a local one.

---

## 4. Why the handler's first instructions must be in assembly

We want to handle interrupts in C, because that's where we can print
messages and make decisions. But the address in the IDT can't point
straight at a C function, for three reasons:

1. **A C function doesn't save every register.** According to the calling
   convention, a C function may freely change `EAX`, `ECX`, `EDX` and the
   flags, because its caller expects that. As we saw, an interrupted
   program doesn't.
2. **A C function ends with `ret`, not `iret`.** `ret` pops only `EIP`,
   leaving `CS` and `EFLAGS` on the stack. Everything after that would be
   off.
3. **A C function doesn't know about the error code**, and doesn't know
   which vector it was called for.

So the first instructions of every handler are written in assembly. They
save all the registers, call a C function, restore the registers and
`iret`. The C function is ordinary C, and it can be as complicated as we
like.

---

## 5. The design: 256 stubs, one common path, one C function

### The problem with one handler for everything

We'd like to write the save/call/restore code only once. But there are two
problems:

- **The CPU doesn't tell the handler which vector fired.** Nothing on the
  stack and no register contains the vector number. If all 256 gates
  pointed to the same code, it couldn't tell a divide error from a
  keyboard press.
- **The stack doesn't look the same for every vector.** Some exceptions
  push an error code and some don't, so the saved `EIP` is sometimes at
  `[esp]` and sometimes at `[esp + 4]`.

### The solution: a tiny stub per vector

So each vector gets its own small piece of code, called a **stub**, that
fixes both problems and then jumps to the common code:

1. If the CPU didn't push an error code for this vector, the stub pushes a
   **dummy error code of 0**. Now every stack looks the same.
2. The stub pushes **its vector number**. Now the common code knows which
   vector fired.
3. It jumps to the common code, `trap_common`.

```
 vector 0  ─► stub0:   push 0 ; push 0  ; jmp trap_common ──┐
 vector 3  ─► stub3:   push 0 ; push 3  ; jmp trap_common ──┤
 vector 8  ─► stub8:            push 8  ; jmp trap_common ──┤   CPU already pushed
 vector 14 ─► stub14:           push 14 ; jmp trap_common ──┤   an error code
   ...                                                      │
                                                            ▼
                      trap_common:  save all the registers
                                    call trap(pointer to the saved state)   ← C
                                    restore all the registers
                                    remove the vector number and error code
                                    iret
```

Here are the real stubs for vectors 0 and 8, as assembled:

```
00100f80 <stub0>:
  100f80:   6a 00              push   0x0          dummy error code
  100f82:   6a 00              push   0x0          vector number 0
  100f84:   e9 36 0a 00 00     jmp    1019bf <trap_common>

00100fc8 <stub8>:
  100fc8:   6a 08              push   0x8          vector number 8
  100fca:   e9 f0 09 00 00     jmp    1019bf <trap_common>
```

(`push 0x0` is encoded in 2 bytes, but it still pushes a full 4-byte
value.)

### Generating 256 stubs with NASM macros

Writing 256 stubs by hand would be tedious and error-prone. NASM has a
macro language that can generate them. If you haven't used it before,
here's what you need:

- **`%macro NAME 1` ... `%endmacro`** defines a macro that takes one
  argument. Inside it, **`%1`** is replaced by the argument.
- **`%if` / `%else` / `%endif`** includes code only if a condition is true.
  The condition is evaluated by the assembler, not at run time.
- **`%assign i 0`** sets an assembler variable, and **`%rep 256` ...
  `%endrep`** repeats a block 256 times.
- **`stub %+ i`** glues `stub` and the value of `i` into one name, like
  `stub3`.

The stub macro uses the list of vectors with error codes from section 2:

```nasm
%macro STUB 1
stub%1:
%if %1 == 8 || (%1 >= 10 && %1 <= 14) || %1 == 17 || %1 == 21 || %1 == 29 || %1 == 30
    ; the CPU already pushed an error code
%else
    push dword 0                ; dummy error code, so all frames look alike
%endif
    push dword %1               ; the vector number
    jmp trap_common
%endmacro
```

Then a loop calls it 256 times:

```nasm
%assign i 0
%rep 256
    STUB i
%assign i i + 1
%endrep
```

The C code that fills the IDT needs the address of each stub. A second loop
builds a table of the 256 addresses, `trap_stubs`, in the `.rodata`
section. `dd` emits a 4-byte value (lesson 0):

```nasm
section .rodata
global trap_stubs
trap_stubs:
%assign i 0
%rep 256
    dd stub %+ i
%assign i i + 1
%endrep
```

In C, it's declared as `extern uint32_t trap_stubs[256];`, and
`trap_stubs[14]` is the address of `stub14`.

### `trap_common`: saving and restoring everything

When `trap_common` starts, the CPU and the stub have pushed five values
(`EFLAGS`, `CS`, `EIP`, the error code, the vector number). Now
`trap_common` saves everything else:

```nasm
trap_common:
    push ds                     ; the four data segment registers
    push es
    push fs
    push gs
    pushad                      ; the eight general-purpose registers
```

**`pushad`** ("push all doubleword") pushes the eight general-purpose
registers in one instruction, in this order: `EAX`, `ECX`, `EDX`, `EBX`,
`ESP`, `EBP`, `ESI`, `EDI`. The `ESP` it pushes is the value from *before*
`pushad` started. Its counterpart **`popad`** pops them all back, except
that it throws away the saved `ESP` (restoring `ESP` from the stack in the
middle of popping would make no sense).

Next, `trap_common` makes sure the data segment registers hold the
kernel's data segment:

```nasm
    mov ax, 0x10                ; kernel data selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
```

Right now they already do, because only the kernel runs. But from lesson
10, an interrupt can arrive while a user program runs, and then `DS` holds
the *user* data segment. The CPU switches `CS` for us (from the IDT gate)
but not the data segment registers, so we do it ourselves.

Then it calls the C function. Its argument is a pointer to everything we
just saved, which is simply the current value of `ESP`: the saved values
start at the top of the stack. So we push `ESP` itself as the argument:

```nasm
    push esp                    ; argument: pointer to the saved state
    call trap
    add esp, 4                  ; remove the argument
```

When `trap` returns, everything is undone in exactly the reverse order:

```nasm
global trapret
trapret:
    popad
    pop gs
    pop fs
    pop es
    pop ds
    add esp, 8                  ; remove the vector number and error code
    iretd                       ; pop EIP, CS, EFLAGS and continue
```

`add esp, 8` throws away the vector number and the error code, because
`iret` expects `EIP` at the top of the stack. (`iretd` is NASM's name for
the 32-bit `iret`.)

The label `trapret` is made global, and it's not just for show: in lesson
10, the kernel will start a user program for the first time by building a
fake set of saved registers on the stack and jumping to `trapret`. The
`iret` at the end then "returns" into the program.

---

## 6. The trap frame: the saved state, as a C structure

### The stack, fully built

Here is the complete stack when `trap()` is called for our division by
zero, with every address and value read from GDB. Start at the top (the
higher addresses, pushed first) and go down:

```
address     value         pushed by          field name
0x107864    0x00010046    CPU                eflags
0x107860    0x00000008    CPU                cs
0x10785c    0x00100f4c    CPU                eip       (the div instruction)
0x107858    0x00000000    stub0              err       (dummy error code)
0x107854    0x00000000    stub0              trapno    (vector 0)
0x107850    0x00000010    trap_common        ds
0x10784c    0x00000010    trap_common        es
0x107848    0x00000010    trap_common        fs
0x107844    0x00000010    trap_common        gs
0x107840    0x00001234    pushad             eax
0x10783c    0x00000000    pushad             ecx
0x107838    0x00000000    pushad             edx
0x107834    0x00009500    pushad             ebx
0x107830    0x00107844    pushad             esp       (ESP before pushad)
0x10782c    0x00107880    pushad             ebp
0x107828    0x00000000    pushad             esi
0x107824    0x00001000    pushad             edi       ◄── ESP after pushad
0x107820    0x00107824    push esp           the argument of trap()
0x10781c    ...           call trap          return address into trap_common
```

These are the real register values of the interrupted code. `EAX` is
`0x1234` because the test code puts that number in `EAX` just before the
division, as a marker. `EBX` is `0x9500`: the Multiboot information
address, still there from `_start` in lesson 0. And the saved `ESP` is
`0x107844`, which is the address just above where `pushad` started.

### Reading the stack as a structure

`trap()` receives `0x107824`, the address of the **last** value pushed.
Now here's the key idea of this lesson: **a C structure is just a way of
reading memory at fixed offsets.** If we define a structure whose fields
are in the order of the values in memory, *starting from the lowest
address*, then `tf->eax` reads exactly the right slot.

Because the stack grows downwards, the lowest address holds the value
pushed last. So **the structure lists the fields in the reverse of the
order they were pushed**:

```c
struct trapframe {
    /* pushed by pushad (in reverse: it pushes eax first, edi last) */
    uint32_t edi, esi, ebp;
    uint32_t esp_unused;        /* ESP before pushad; popad ignores it */
    uint32_t ebx, edx, ecx, eax;

    /* pushed by trap_common (ds first, gs last) */
    uint32_t gs, fs, es, ds;

    /* pushed by the stub */
    uint32_t trapno;
    uint32_t err;

    /* pushed by the CPU */
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;

    /* pushed by the CPU only when coming from ring 3 (lesson 10) */
    uint32_t user_esp;
    uint32_t user_ss;
};
```

Check it against the stack: the first field, `edi`, is at offset 0, so at
`0x107824`, where we find `0x1000`. The field `eax` is the eighth, at
offset 28 = `0x1c`, so at `0x107824 + 0x1c = 0x107840`, where we find
`0x1234`. And `eip` is the 15th, at offset 56 = `0x38`, at `0x10785c`, where
we find `0x100f4c`. GDB agrees:

```
(gdb) p/x *tf
$1 = {edi = 0x1000, esi = 0x0, ebp = 0x107880, esp_unused = 0x107844,
      ebx = 0x9500, edx = 0x0, ecx = 0x0, eax = 0x1234, gs = 0x10, fs = 0x10,
      es = 0x10, ds = 0x10, trapno = 0x0, err = 0x0, eip = 0x100f4c,
      cs = 0x8, eflags = 0x10046, user_esp = 0x1021ff, user_ss = 0x0}
```

The last two fields, `user_esp` and `user_ss`, are pushed by the CPU only
when the interrupt comes from a user program (lesson 10). In this lesson,
there's nothing pushed there, so those two "fields" are just whatever was
on the stack above the frame: `0x1021ff` is meaningless. **Never read them
unless the interrupt came from ring 3**, which you can tell from
`(tf->cs & 3) == 3`.

### The structure isn't a copy

This is worth stopping on, because it's what makes system calls possible.
`tf` points *into the stack*, at the very values that `trapret` will pop
back into the registers. So if `trap()` writes `tf->eax = 42`, then when
`popad` runs, `EAX` becomes 42, and the interrupted code sees 42 in `EAX`
when it continues.

That's how, in lesson 11, the kernel will return a value from a system
call: the program puts a system call number in `EAX`, executes `int 0x80`,
the kernel reads `tf->eax`, does the work, writes the result into
`tf->eax`, and returns. Exercise 1 lets you try it now.

---

## 7. The C side: `trap()`

Every interrupt now ends up in one C function:

```c
void trap(struct trapframe *tf);
```

For now it only needs to handle two cases:

- **The breakpoint (vector 3).** Print the saved `EIP` and return.
  Because a breakpoint is a trap, returning continues with the next
  instruction.
- **Everything else.** Print the whole trap frame and `panic`. We can't fix
  a divide error or a general protection fault, and as section 2 explained,
  returning from a fault would just run the same instruction again.

A `switch` on `tf->trapno` is the natural shape, because it will grow a
case in many later lessons: the clock and keyboard (lesson 5), page faults
(lesson 7), system calls (lesson 11), and errors in user programs, which
should kill the program rather than the kernel (lesson 12).

For the frame dump, it helps to print the exception's name along with its
number. A table of 32 strings, indexed by the vector number, does it:
`"divide error"`, `"debug"`, `"NMI"`, `"breakpoint"`, and so on. You can
find the full list in the Intel manual (Volume 3A, Table 6-1) or in the
reference code below.

Give the vectors you handle specially names in `trap.h`, like
`#define T_BREAKPOINT 3`, so the `switch` doesn't contain bare numbers.

---

## 8. Writing it

You'll write five new files in `kernel/arch/i386/`, and change `kmain.c`.
The order below lets you test after each step.

### Step 1: `trap.h` and `trap.c`

`trap.h` contains `struct trapframe` (section 6), the vector names, and the
declaration of `trap()`. `trap.c` contains the table of exception names, a
`dump_trapframe()` function that prints every field, and `trap()` itself.

When printing the segment registers, print only their low 16 bits
(`tf->ds & 0xFFFF`). They were pushed as 4-byte values, but a segment
register is 16 bits, and on some CPUs the top half of the pushed value
isn't zero.

**Check:** `make` compiles. Nothing calls `trap()` yet.

### Step 2: one stub, `trap_common`, and one gate

In `isr.asm`, write `stub0` by hand (no macros yet), and `trap_common` with
`trapret`. Make `stub0` global so C can find it.

In `idt.c`, write the two packed structures (the gate and the 6-byte IDTR
operand), the table `static struct idt_gate idt[256];`, and two functions:

- `idt_set_gate(vector, handler, dpl)`, which fills in one gate: the two
  halves of the address, selector `SEG_KCODE` from `gdt.h`, zero, and
  `0x8E | (dpl << 5)`. The DPL is shifted into bits 5 and 6.
- `idt_init()`, which for now sets only gate 0 to the address of `stub0`,
  then runs `lidt`. You can do `lidt` with inline assembly; the `"m"`
  constraint passes the structure's memory location, like `sgdt` in
  lesson 3:

  ```c
  __asm__ volatile ("lidt %0" : : "m"(idtr));
  ```

Put `idt_init()`'s declaration in `idt.h`. In `kmain`, call `gdt_init()`
then `idt_init()`, and then divide by zero.

**How to divide by zero on purpose.** Don't write it in C. Dividing by zero
is undefined behavior in C, so GCC may compile it into something that
doesn't divide at all. Lesson 0's exercise showed this: GCC replaced `1 /
zero` with a comparison. Use inline assembly, so the CPU gets exactly the
instruction you want:

```c
__asm__ volatile ("mov $0x1234, %%eax; xor %%edx, %%edx; xor %%ecx, %%ecx; div %%ecx"
                  : : : "eax", "ecx", "edx");
```

This puts `0x1234` in `EAX` (a marker, so you can check the dump shows the
real registers), clears `EDX` and `ECX`, and divides `EDX:EAX` by `ECX`. The
list after the third colon is the **clobber list**: it tells GCC that these
three registers are changed by the assembly, so it mustn't keep anything
in them across it.

**Check:** run it. Instead of a reset, you should get the dump, with
`trap 0 (divide error)`, `eax=00001234` and `PANIC: unhandled trap 0`.
Find the `eip` value in the disassembly, or turn it into a source line:

```
$ i686-elf-addr2line -e build/kernel.elf 0x00100f4c
kernel/kmain.c:30
```

### Step 3: all 256 stubs and gates

Replace the hand-written `stub0` with the `STUB` macro and the two `%rep`
loops from section 5. In `idt_init()`, set all 256 gates from the
`trap_stubs` table, with DPL 0.

Then add the breakpoint test in `kmain`, before the division:

```c
__asm__ volatile ("int3");
kprintf("back in kmain after int3\n");
```

and the `T_BREAKPOINT` case in `trap()`.

**Check:** the full milestone output. The breakpoint is handled and `kmain`
continues; then the division by zero is reported. In the dump, the `cs`
value is `0008` (the kernel code segment from lesson 3), and the `eflags`
value `00010046` has bit 9 (`IF`, interrupts enabled) at 0, because we
haven't turned interrupts on yet. Bit 16, which is set, is the "resume
flag" the CPU uses internally for faults; you can ignore it.

<details>
<summary>Reference: <code>trap.h</code></summary>

```c
/* trap.h -- the trap frame: the CPU state saved when an interrupt happens.
 *
 * isr.asm builds this on the stack, then calls trap(tf). Field order is the
 * REVERSE of push order (the stack grows down, a struct grows up):
 *
 *      pushed by the CPU:   [ss, esp only if coming from ring 3], eflags, cs, eip
 *      pushed by the stub:  err (dummy 0 if the CPU pushed none), trapno
 *      pushed by trap_common: ds, es, fs, gs, then PUSHAD
 *                             (eax, ecx, edx, ebx, esp, ebp, esi, edi)
 */
#ifndef ARCH_TRAP_H
#define ARCH_TRAP_H

#include <stdint.h>

struct trapframe {
    /* PUSHAD */
    uint32_t edi, esi, ebp;
    uint32_t esp_unused;        /* ESP before PUSHAD; POPAD ignores it */
    uint32_t ebx, edx, ecx, eax;

    /* segment registers (pushed as 32-bit values) */
    uint32_t gs, fs, es, ds;

    /* pushed by the per-vector stub */
    uint32_t trapno;
    uint32_t err;

    /* pushed by the CPU */
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;

    /* pushed by the CPU only when coming from ring 3 */
    uint32_t user_esp;
    uint32_t user_ss;
};

/* Exception vectors we treat specially */
#define T_DIVIDE     0
#define T_BREAKPOINT 3
#define T_GPFAULT    13
#define T_PAGEFAULT  14

void trap(struct trapframe *tf);

#endif
```

</details>

<details>
<summary>Reference: <code>trap.c</code></summary>

```c
/* trap.c -- every interrupt and exception ends up in trap(). */
#include "arch/i386/trap.h"
#include "lib/kprintf.h"
#include "lib/panic.h"

static const char *exception_names[32] = {
    "divide error", "debug", "NMI", "breakpoint", "overflow",
    "bound range", "invalid opcode", "device not available",
    "double fault", "coprocessor overrun", "invalid TSS",
    "segment not present", "stack fault", "general protection",
    "page fault", "reserved", "x87 FPU error", "alignment check",
    "machine check", "SIMD error", "virtualization", "control protection",
    "reserved", "reserved", "reserved", "reserved", "reserved", "reserved",
    "hypervisor injection", "VMM communication", "security", "reserved",
};

static void dump_trapframe(struct trapframe *tf)
{
    kprintf("trap %lu (%s), err=%lx\n", tf->trapno,
            tf->trapno < 32 ? exception_names[tf->trapno] : "interrupt", tf->err);
    kprintf("  eip=%08lx cs=%04lx eflags=%08lx\n", tf->eip, tf->cs, tf->eflags);
    kprintf("  eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx\n", tf->eax, tf->ebx, tf->ecx, tf->edx);
    kprintf("  esi=%08lx edi=%08lx ebp=%08lx\n", tf->esi, tf->edi, tf->ebp);
    kprintf("  ds=%04lx es=%04lx fs=%04lx gs=%04lx\n",
            tf->ds & 0xFFFF, tf->es & 0xFFFF, tf->fs & 0xFFFF, tf->gs & 0xFFFF);
}

void trap(struct trapframe *tf)
{
    switch (tf->trapno) {
    case T_BREAKPOINT:
        /* INT3 is a "trap": the saved EIP is already past the instruction,
         * so simply returning continues the program. */
        kprintf("breakpoint at eip=%08lx, continuing\n", tf->eip);
        return;

    default:
        dump_trapframe(tf);
        panic("unhandled trap %lu", tf->trapno);
    }
}
```

</details>

<details>
<summary>Reference: <code>isr.asm</code></summary>

```nasm
; isr.asm -- the first instructions run for EVERY interrupt and exception.
;
; There are 256 tiny stubs, one per vector. Each makes the stack look the
; same (push an error code if the CPU didn't, then push the vector number)
; and jumps to trap_common, which saves the remaining registers, calls the
; C function trap(struct trapframe *), then restores everything and IRETs.

extern trap

SEG_KDATA equ 0x10

; Vectors for which the CPU itself pushes an error code.
%macro STUB 1
stub%1:
%if %1 == 8 || (%1 >= 10 && %1 <= 14) || %1 == 17 || %1 == 21 || %1 == 29 || %1 == 30
    ; error code already on the stack
%else
    push dword 0                ; dummy error code, so all frames look alike
%endif
    push dword %1               ; vector number -> trapframe.trapno
    jmp trap_common
%endmacro

section .text

%assign i 0
%rep 256
    STUB i
%assign i i + 1
%endrep

trap_common:
    push ds
    push es
    push fs
    push gs
    pushad

    mov ax, SEG_KDATA           ; we may have come from user mode:
    mov ds, ax                  ; make sure we use kernel data segments
    mov es, ax
    mov fs, ax
    mov gs, ax

    push esp                    ; argument: pointer to the trapframe
    call trap
    add esp, 4

; Returning from ANY interrupt goes through here. Lesson 10 also jumps here
; to enter user mode for the first time.
global trapret
trapret:
    popad
    pop gs
    pop fs
    pop es
    pop ds
    add esp, 8                  ; drop trapno and err
    iretd

; Table of stub addresses, used by idt.c to fill the IDT.
section .rodata
global trap_stubs
trap_stubs:
%assign i 0
%rep 256
    dd stub %+ i
%assign i i + 1
%endrep
```

</details>

<details>
<summary>Reference: <code>idt.h</code> and <code>idt.c</code></summary>

```c
/* idt.h -- Interrupt Descriptor Table. */
#ifndef ARCH_IDT_H
#define ARCH_IDT_H

#include <stdint.h>

void idt_init(void);

/* Who may trigger the vector with an INT instruction: dpl 0 = kernel only,
 * dpl 3 = user programs too (the system call vector, lesson 11). */
void idt_set_gate(int vector, uint32_t handler, int dpl);

#endif
```

```c
/* idt.c -- the Interrupt Descriptor Table: vector number -> handler address.
 *
 * Every vector points at its stub in isr.asm. All stubs end up in trap(),
 * so the IDT never changes after boot except for the DPL of vector 0x80.
 */
#include "arch/i386/idt.h"
#include "arch/i386/gdt.h"

struct idt_gate {
    uint16_t offset_low;    /* handler address bits 0..15                 */
    uint16_t selector;      /* code segment to run the handler in         */
    uint8_t  zero;
    uint8_t  type_attr;     /* P | DPL (2 bits) | 0 | gate type (4 bits)  */
    uint16_t offset_high;   /* handler address bits 16..31                */
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

/* P=1, type 0xE = 32-bit interrupt gate (the CPU clears IF on entry) */
#define GATE_INTERRUPT 0x8E

static struct idt_gate idt[256];
static struct idt_ptr  idtr;

extern uint32_t trap_stubs[256];   /* isr.asm */

void idt_set_gate(int vector, uint32_t handler, int dpl)
{
    idt[vector].offset_low  = handler & 0xFFFF;
    idt[vector].selector    = SEG_KCODE;
    idt[vector].zero        = 0;
    idt[vector].type_attr   = GATE_INTERRUPT | (dpl << 5);
    idt[vector].offset_high = handler >> 16;
}

void idt_init(void)
{
    for (int i = 0; i < 256; i++)
        idt_set_gate(i, trap_stubs[i], 0);

    idtr.limit = sizeof(idt) - 1;
    idtr.base  = (uint32_t)idt;
    __asm__ volatile ("lidt %0" : : "m"(idtr));
}
```

</details>

<details>
<summary>Reference: <code>kmain.c</code></summary>

```c
/* kmain.c -- the kernel's C entry point, called from boot.asm. */
#include <stdint.h>
#include "arch/i386/gdt.h"
#include "arch/i386/idt.h"
#include "arch/i386/multiboot.h"
#include "dev/console.h"
#include "lib/kprintf.h"
#include "lib/panic.h"

void kmain(uint32_t magic, uint32_t mbi_phys)
{
    (void)mbi_phys;

    console_init();
    kprintf("toyos booting...\n");
    if (magic != MULTIBOOT_BOOTLOADER_MAGIC)
        panic("not booted by a Multiboot loader (magic=%lx)", magic);

    gdt_init();
    idt_init();
    kprintf("GDT and IDT loaded\n");

    /* Test 1: an exception we handle and return from. */
    __asm__ volatile ("int3");
    kprintf("back in kmain after int3\n");

    /* Test 2: an exception we can't recover from. Written in assembly
     * because GCC is allowed to compile a C division by zero into anything. */
    kprintf("dividing by zero...\n");
    __asm__ volatile ("mov $0x1234, %%eax; xor %%edx, %%edx; xor %%ecx, %%ecx; div %%ecx"
                      : : : "eax", "ecx", "edx");

    panic("not reached");
}
```

The reference drops lesson 2's and 3's printing from `kmain` to keep the
output short. You can keep yours.

</details>

---

## 9. Exercises

1. **Change a register from the handler.** In the `T_BREAKPOINT` case, add
   `tf->eax = 0xCAFE;`. In `kmain`, read `EAX` right after the `int3`:

   ```c
   uint32_t result;
   __asm__ volatile ("mov $1, %%eax; int3" : "=a"(result));
   kprintf("back in kmain after int3, eax=%lx\n", result);
   ```

   (`"=a"(result)` means: after the assembly, copy `EAX` into `result`.)
   You should see `eax=cafe`, not `1`. The handler changed a register of
   the interrupted code just by writing to the trap frame. This is exactly
   how system calls will return their results in lesson 11.

2. **Forget the dummy error code.** In the `STUB` macro, delete the line
   `push dword 0`. Before running, predict what goes wrong for `int3`.
   Then run with the QEMU log on.

   <details>
   <summary>What happens</summary>

   ```
   breakpoint at eip=00000008, continuing
   trap 13 (general protection), err=0
     eip=001017ed cs=0008 eflags=00010016
   ```

   Without the dummy error code, the frame is one value short, so the
   values the CPU pushed are one slot off from where the structure expects
   them. `tf->trapno` is still right (3), but `tf->err` reads the CPU's
   `EIP`, and `tf->eip` reads the slot where the CPU put `CS`, so the
   handler reports `eip=00000008`.

   Then `trapret` removes 8 bytes, as usual. But those 8 bytes were the
   vector number and the CPU's `EIP`, so `iret` now pops the wrong values:
   the saved `CS` (8) as `EIP`, the saved `EFLAGS` (`0x2`) as `CS`, and
   whatever was above them as `EFLAGS`. `0x0002` is the null selector (lesson
   3, section 5), which isn't allowed in `CS`, so `iret` itself raises a
   general protection fault. Its address, `0x1017ed`, is the `iretd`
   instruction in `trapret`:

   ```
   $ i686-elf-objdump -d -M intel build/kernel.elf | grep 1017ed
     1017ed:   cf     iret
   ```

   This is the classic symptom of an error-code mistake: the crash happens
   at the `iret`, not where the interrupt came from.

   </details>

3. **Return from a fault.** Add a `T_DIVIDE` case to `trap()` that prints a
   message and returns, instead of panicking. Run it. What happens, and
   why? (Section 2 has the answer.)

   <details>
   <summary>What happens</summary>

   ```
   divide error at eip=00100f8c, returning
   divide error at eip=00100f8c, returning
   divide error at eip=00100f8c, returning
   ...
   ```

   The message repeats forever: in two seconds the reference kernel printed
   it almost 4000 times. A divide error is a fault, so the saved `EIP`
   points to the `div` itself, and returning runs it again with the same
   `ECX` of 0.

   </details>

4. **Fix the fault from the handler.** Building on exercise 3: instead of
   just returning, set `tf->ecx = 1` in the `T_DIVIDE` case. Now what does
   the `div` do when it's retried? What's in `EAX` afterwards? (The retried
   `div` divides `0x1234` by 1, so `EAX` is `0x1234` and `kmain` carries
   on. You've just written a handler that repairs the cause of a fault and
   lets the program continue, the same idea that demand paging is built
   on.)

5. **A different exception.** Replace the division with the instruction
   `ud2`, which exists only to be invalid. Which vector do you get? Is the
   saved `EIP` the address of `ud2` or the instruction after it? (Vector 6,
   invalid opcode. It's a fault, so `EIP` is the address of `ud2` itself,
   which you can check with `i686-elf-objdump -d -M intel
   build/kernel.elf | grep ud2`.)

## Pitfalls

- **A wrong list of error-code vectors.** For every vector on the wrong
  side of the list, the frame is off by 4 bytes, and the crash happens at
  `iret` (exercise 2).
- **Ending a handler with `ret` instead of `iret`**, or forgetting
  `add esp, 8` before `iret`.
- **A gate whose selector isn't your kernel code selector.** The CPU can't
  even start the handler: general protection fault, then double fault,
  then reset.
- **Returning from a fault without fixing its cause.** The same fault
  repeats forever (exercise 3).
- **Reading `tf->user_esp` or `tf->user_ss` for an interrupt that came from
  the kernel.** Those slots aren't part of the frame then.
- **Dividing by zero in C to test the handler.** GCC is allowed to compile
  it into something else. Use inline assembly.
- **The QEMU log shows a second exception while starting the first one's
  handler** (`check_exception old: 0x0 new 0xd`, for example): the
  handler itself couldn't be started. Check the IDT entry's address,
  selector and type.

## References

- Intel® 64 and IA-32 Architectures Software Developer's Manual, Volume 3A,
  chapter 6 "Interrupt and Exception Handling": 6.3 (where interrupts come
  from), 6.5 (faults, traps and aborts), 6.10–6.11 (the IDT and its gates),
  6.12 (what the CPU pushes: Figure 6-4), 6.13 (error codes), and Table 6-1
  (every exception, and which ones push an error code).
- OSDev wiki: "Interrupts", "Interrupt Descriptor Table", "Exceptions",
  "Interrupt Service Routines".
- xv6: `vectors.pl` (a script that generates the stubs), `trapasm.S`
  (`alltraps` and `trapret`), `trap.c`, and `struct trapframe` in `x86.h`.
  The design is the same as ours, and reading it is good practice.
