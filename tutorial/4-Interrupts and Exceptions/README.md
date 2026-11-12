# 4 — Interrupts and Exceptions

**Goal:** build the one path that **every** interrupt, exception and (later)
system call goes through. Get this right once and lessons 5, 10, 11 and
24 are small additions to it.

**Milestone:**

```
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

`i686-elf-addr2line -e build/kernel.elf 00100f4c` → `kmain.c:30`, the `div`.

**Reference code:** [`code/`](code/). New: [`isr.asm`](code/kernel/arch/i386/isr.asm),
[`trap.h`](code/kernel/arch/i386/trap.h), [`trap.c`](code/kernel/arch/i386/trap.c),
[`idt.h`](code/kernel/arch/i386/idt.h), [`idt.c`](code/kernel/arch/i386/idt.c).

---

## 1. The mental model

Forget the jargon for a moment. **An interrupt is a function call that the
CPU makes on its own**, between two instructions of whatever was running.

| A normal `call handler` | An interrupt |
|---|---|
| You decide when | The CPU decides (a key was pressed, you divided by zero, you executed `int 0x80`) |
| Target address is in the instruction | Target address is looked up in a table, the **IDT**, by **vector number** (0–255) |
| Pushes: return address | Pushes: **EFLAGS, CS, EIP** (+ an error code for some exceptions) (+ SS, ESP if it came from user mode) |
| Return with `ret` | Return with **`iret`**, which pops EIP, CS, EFLAGS (+ ESP, SS) |
| Caller expects some registers clobbered | **Nobody** expects it: the handler must save and restore **every** register |

That last row is the whole reason for the assembly: the interrupted code
didn't ask to be interrupted. If your handler changes EAX and doesn't
restore it, some random line of C somewhere gets a wrong value. So the
handler's first job is to save everything, and its last job is to restore
everything.

There are three kinds of interrupt, all handled the same way:

| Kind | Vectors | Cause | Example |
|---|---|---|---|
| **Exception** | 0–31 | the CPU itself, because of the current instruction | 0 divide error, 13 general protection, 14 page fault |
| **Hardware IRQ** | 32–47 (after remapping, lesson 5) | a device | timer, keyboard, disk |
| **Software interrupt** | any, we use 0x80 | the `int N` instruction | system calls (lesson 11) |

### The design: 256 stubs → one common path → one C function

```
 vector 0   ─► stub0:   push 0; push 0;  jmp trap_common ─┐
 vector 3   ─► stub3:   push 0; push 3;  jmp trap_common ─┤
 vector 14  ─► stub14:          push 14; jmp trap_common ─┤  (CPU pushed the error code)
 vector 32  ─► stub32:  push 0; push 32; jmp trap_common ─┤
 vector 128 ─► stub128: push 0; push 128;jmp trap_common ─┤
                                                          ▼
                            trap_common: save all registers
                                         call trap(struct trapframe *tf)   ← C
                                         restore all registers
                                         iret
```

The IDT maps each vector to its stub. Each stub exists only to make the stack
look **identical** for every vector: some exceptions push an error code
and some don't, so the stub pushes a fake 0 when the CPU didn't. Then it pushes
the vector number, because after this point nothing else remembers which
vector fired.

From then on everything is shared, and in C you get one function,
`trap(tf)`, that sees a struct describing the interrupted CPU state.

## 2. The trap frame, byte by byte

This is the picture to keep in mind. Suppose kernel code is running with
`ESP = 0x00107900` when the divide error fires. Addresses go **down** as
things are pushed:

```
 address      pushed by        value               trapframe field
 ─────────    ─────────────    ─────────────────   ──────────────
 0x001078FC   CPU              EFLAGS              eflags
 0x001078F8   CPU              CS = 0x08           cs
 0x001078F4   CPU              EIP of the div      eip
 0x001078F0   stub0            0 (fake error)      err
 0x001078EC   stub0            0 (vector)          trapno
 0x001078E8   trap_common      DS                  ds
 0x001078E4   trap_common      ES                  es
 0x001078E0   trap_common      FS                  fs
 0x001078DC   trap_common      GS                  gs
 0x001078D8   PUSHAD           EAX                 eax
 0x001078D4   PUSHAD           ECX                 ecx
 0x001078D0   PUSHAD           EDX                 edx
 0x001078CC   PUSHAD           EBX                 ebx
 0x001078C8   PUSHAD           ESP (before pushad) esp_unused
 0x001078C4   PUSHAD           EBP                 ebp
 0x001078C0   PUSHAD           ESI                 esi
 0x001078BC   PUSHAD           EDI                 edi    ◄── ESP now = 0x001078BC
```

Now `push esp; call trap`: the argument is `0x001078BC`, the address of the
**last** thing pushed. A C struct's first field is at the lowest address,
so the struct lists the fields **in reverse push order**:

```c
struct trapframe {
    uint32_t edi, esi, ebp, esp_unused, ebx, edx, ecx, eax;   /* PUSHAD  */
    uint32_t gs, fs, es, ds;                                   /* ours    */
    uint32_t trapno, err;                                      /* stub    */
    uint32_t eip, cs, eflags;                                  /* CPU     */
    uint32_t user_esp, user_ss;      /* CPU, only from ring 3 (lesson 10) */
};
```

**There's no magic.** The struct is just a way to look at the stack.
Write `tf->eax = 42` in `trap()` and, when `trap_common` does `popad`, EAX
*becomes* 42 in the interrupted code. That's exactly how a system call
returns a value (lesson 11): the "aha" of this lesson.

The last two fields only exist if the interrupt came from ring 3. In
lesson 10 we'll see why the CPU pushes them then. For a kernel-mode
interrupt they're whatever was on the stack before: never read them unless
`(tf->cs & 3) == 3`.

### `trap_common`, annotated

```nasm
trap_common:
    push ds                     ; save the data segment registers
    push es
    push fs
    push gs
    pushad                      ; save eax ecx edx ebx esp ebp esi edi

    mov ax, 0x10                ; if we came from user mode, DS etc. are user
    mov ds, ax                  ; segments; the kernel wants its own
    mov es, ax
    mov fs, ax
    mov gs, ax

    push esp                    ; trap(struct trapframe *tf)
    call trap
    add esp, 4                  ; pop the argument

global trapret
trapret:                        ; everything below exactly undoes the above
    popad
    pop gs
    pop fs
    pop es
    pop ds
    add esp, 8                  ; drop trapno and err (iret doesn't expect them)
    iretd                       ; pop eip, cs, eflags (+ esp, ss if returning to ring 3)
```

The `trapret` label is public because in lesson 10 you'll enter user mode for
the first time by **building a fake trap frame and jumping to `trapret`**.

### Generating 256 stubs with NASM

Writing 256 stubs by hand is silly; a NASM macro and `%rep` generate them.
The list of vectors where the CPU pushes an error code is
**8, 10, 11, 12, 13, 14, 17, 21, 29, 30** (Intel SDM Vol. 3A, Table 6-1):

```nasm
%macro STUB 1
stub%1:
%if %1 == 8 || (%1 >= 10 && %1 <= 14) || %1 == 17 || %1 == 21 || %1 == 29 || %1 == 30
%else
    push dword 0
%endif
    push dword %1
    jmp trap_common
%endmacro

%assign i 0
%rep 256
    STUB i
%assign i i + 1
%endrep
```

A second `%rep` emits a table `trap_stubs` of their addresses, so C can
fill the IDT in a loop.

## 3. The IDT

An array of 256 8-byte **gates**. A gate says: "for this vector, jump to
this address with this CS, if the caller is privileged enough":

```
 bits:  offset 31..16 | P | DPL | 0 | type | 000 | 00000 | selector | offset 15..0
```

| Field | Our value | Meaning |
|---|---|---|
| offset | address of `stubN` | where to jump |
| selector | `0x08` | run the handler in kernel code segment = **ring 0** |
| type | `0xE` | 32-bit **interrupt gate**: the CPU clears IF on entry, so a second interrupt can't arrive in the middle of saving registers. (`0xF` = trap gate: IF unchanged.) |
| DPL | `0` (vector 0x80 will be `3`) | the minimum privilege needed to fire it **with an `int` instruction**. Hardware interrupts and exceptions ignore it. |
| P | 1 | present |

So `type_attr = 0x8E` for kernel-only, `0xEE` for the syscall gate.
From [`idt.c`](code/kernel/arch/i386/idt.c):

```c
void idt_set_gate(int vector, uint32_t handler, int dpl)
{
    idt[vector].offset_low  = handler & 0xFFFF;
    idt[vector].selector    = SEG_KCODE;
    idt[vector].zero        = 0;
    idt[vector].type_attr   = GATE_INTERRUPT | (dpl << 5);
    idt[vector].offset_high = handler >> 16;
}
```

`lidt` takes the same 6-byte `{limit, base}` operand as `lgdt`.

## 4. `trap()`: what to do with the frame

For now ([`trap.c`](code/kernel/arch/i386/trap.c)): breakpoint prints and
returns; anything else dumps the frame and panics. It will grow:

```c
void trap(struct trapframe *tf)
{
    switch (tf->trapno) {
    case T_BREAKPOINT:   /* return = continue after the int3 */
        ...
        return;
    /* lesson 5:  IRQ 32..47  → device handlers                  */
    /* lesson 7:  T_PAGEFAULT → print CR2                        */
    /* lesson 11: 0x80        → syscall(tf)                      */
    /* lesson 12: exception from ring 3 → kill the process, not the kernel */
    default:
        dump_trapframe(tf);
        panic("unhandled trap %lu", tf->trapno);
    }
}
```

### Faults vs traps: where does EIP point?

- A **fault** (divide error, GP, page fault) saves the EIP **of the faulting
  instruction**. Returning from the handler **re-runs it**. That's what lets
  a page-fault handler map the missing page and retry (demand paging),
  or a program's own signal handler fix things up and retry (lesson 26),
  but for a divide error it means an
  infinite loop if you just return.
- A **trap** (`int3`, `int 0x80`) saves the EIP of the **next** instruction.
  Returning continues after it. That's why our `int3` test resumes `kmain`.

---

## Modules

| File | Responsibility | Exposes |
|---|---|---|
| [`arch/i386/isr.asm`](code/kernel/arch/i386/isr.asm) | 256 stubs, `trap_common`, `trapret` | `trap_stubs[]`, `trapret` |
| [`arch/i386/trap.h`](code/kernel/arch/i386/trap.h) | `struct trapframe`, vector names | the struct; `T_*` constants |
| [`arch/i386/trap.c`](code/kernel/arch/i386/trap.c) | decide what each vector means | `trap(tf)` |
| [`arch/i386/idt.c`](code/kernel/arch/i386/idt.c) | fill and load the IDT | `idt_init()`, `idt_set_gate(vec, addr, dpl)` |
| [`kmain.c`](code/kernel/kmain.c) | `gdt_init(); idt_init();` then the two tests | |

The IDT is installed **once** and never changes (except for the syscall
gate's DPL). New behavior always goes in `trap()`.

## Steps

1. `trap.h` (the struct) and `trap.c` (dump + panic).
2. `isr.asm`: write `stub0` and `trap_common` by hand first, the macros after.
3. `idt.c` with just vector 0 → test divide by zero.
4. Generate all stubs, fill all 256 gates, add the `int3` test.

## Test

- The milestone. Check `eax=00001234`: the frame really holds the
  interrupted code's registers.
- Change the `int3` handler to set `tf->eax = 0xCAFE` and read EAX after
  the `int3` in kmain (inline asm). You just wrote a system call
  mechanism.
- Remove the `push dword 0` in the macro (so no-error-code vectors are
  misaligned) and watch `int3` crash: the `iret` pops the wrong values.

## Pitfalls

- **The C compiler "optimizes away" a divide by zero.** Division by zero is
  undefined behavior, so GCC can compile `1 / x` without a `div` at all (it
  did, here: `1 / zero` printed `0`). Trigger CPU exceptions with inline
  assembly.
- **Error-code list wrong** → every frame for that vector is off by 4 bytes,
  `iret` jumps into the weeds, triple fault.
- **`ret` instead of `iret`** at the end, or forgetting `add esp, 8`.
- **IDT entry selector ≠ your GDT's code selector** → #GP inside the
  handler → double fault → reset.
- **Returning from a fault without fixing the cause** → the same fault
  forever (your log fills with `v=00`).
- **Reading `tf->user_esp` for a kernel-mode trap.** Those slots aren't part
  of the frame then.
- The QEMU log line `check_exception old: 0xffffffff new: 0xd` followed by
  a double fault (`new: 0x8`) means your handler itself faulted.

## References

- Intel SDM Vol. 3A ch. 6 "Interrupt and Exception Handling": §6.3 (sources),
  §6.10 (IDT), §6.11 (gate format), §6.12 (stack picture: Figure 6-4), §6.13
  (error code), Table 6-1 (which vectors push one).
- OSDev: "Interrupts", "IDT", "Exceptions", "Interrupt Service Routines"
- xv6: `vectors.pl` (generates the stubs), `trapasm.S`, `trap.c`, `x86.h`
  (`struct trapframe`). Same design as ours.
