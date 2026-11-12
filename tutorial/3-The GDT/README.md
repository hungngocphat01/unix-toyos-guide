# Lesson 3 — The GDT

This lesson is about **segmentation**, an old x86 memory feature that we
mostly want to switch off, but can't completely. You'll learn what it is,
why it exists, what the **GDT** (Global Descriptor Table) is, and why our
kernel needs a GDT of its own. Then you'll build one and tell the CPU to
use it.

When you're done, the kernel prints the location of the GDT before and
after loading its own:

```
loader's GDT: base=000cb2b8 limit=39 cs=8
our GDT     : base=00102020 limit=39 cs=8
```

The table moved from memory that belongs to the BIOS (`0xcb2b8`) into our
kernel (`0x102020`). That looks like a small change, but this table is what
will make **user mode** possible in lesson 10: the separation between the
kernel, which can do anything, and programs, which can't. It's also needed
for interrupts in lesson 4.

The reference code is in [`code/`](code/). To see every change since
lesson 2:

```sh
cd tutorial
diff -ru "2-Boot Information and the Linker Script/code" "3-The GDT/code"
```

**Contents**

1. [Where segmentation comes from](#1-where-segmentation-comes-from)
2. [Segmentation in 32-bit mode: the descriptor table](#2-segmentation-in-32-bit-mode-the-descriptor-table)
3. [Why we can't ignore it: privilege levels](#3-why-we-cant-ignore-it-privilege-levels)
4. [Why the loader's GDT isn't good enough](#4-why-the-loaders-gdt-isnt-good-enough)
5. [Selectors: what a segment register really holds](#5-selectors-what-a-segment-register-really-holds)
6. [Descriptors: the 8 bytes of a GDT entry](#6-descriptors-the-8-bytes-of-a-gdt-entry)
7. [Telling the CPU where the table is](#7-telling-the-cpu-where-the-table-is)
8. [Reloading the segment registers](#8-reloading-the-segment-registers)
9. [Writing it](#9-writing-it)
10. [Exercises: breaking it on purpose](#10-exercises-breaking-it-on-purpose)

---

## 1. Where segmentation comes from

To understand why segmentation exists, go back to the first x86 CPU, the
Intel 8086 from 1978. It had 16-bit registers, so an address held in one
register could only go from 0 to 65535 (64 KiB). But Intel wanted the CPU
to use up to 1 MiB of memory, which needs 20-bit addresses.

The solution was **segments**. The 8086 got four extra registers, called
**segment registers**: `CS` (code segment), `DS` (data segment), `SS`
(stack segment) and `ES` (extra segment). Every memory access combines a
segment register with a 16-bit offset:

```
address = segment × 16 + offset
```

Multiplying by 16 shifts the segment left by 4 bits, so the result is a
20-bit address. For example, the text screen you've been writing to since
lesson 0 is at `0xB8000`. A 16-bit program would reach it with the segment
`0xB800` and the offset `0`:

```
0xB800 × 16 + 0x0000 = 0xB8000
```

Each segment register picks one 64 KiB window ("segment") of the 1 MiB
space, and the offset picks a byte inside that window. The CPU chooses
which segment register to use from the kind of access: instructions are
fetched through `CS`, stack operations (`push`, `pop`) use `SS`, and most
data accesses use `DS`.

The CPU still starts in this mode today, for backward compatibility. It's
called **real mode**. The bootloader switched the CPU out of it before
running our kernel.

---

## 2. Segmentation in 32-bit mode: the descriptor table

The 80286 and then the 80386 (1985) introduced **protected mode**, which is
the mode our kernel runs in. In 32-bit protected mode, registers are 32
bits wide, so an offset alone can already reach 4 GiB. Segments were no
longer needed to reach more memory, but Intel kept them and gave them a new
job: **protection**. Each segment now comes with rules about who may use it
and how.

In protected mode, a segment register no longer contains a number to
multiply by 16. Instead, it contains an index into a **table of segment
descriptions** stored in memory. That table is the **Global Descriptor
Table**, or **GDT**. Each entry, called a **descriptor**, is 8 bytes and
describes one segment:

- its **base**: the address where the segment starts;
- its **limit**: its size;
- its **privilege level**: who is allowed to use it (section 3);
- its **type**: code or data, read-only or writable, and so on.

When the CPU accesses memory, it takes the offset (the address your program
uses), checks it against the segment's limit, checks that the program is
allowed to use the segment, and adds the segment's base:

```
            mov eax, [0x1000]          uses DS
                    │
   DS = 0x10 ───────┼──► GDT entry 2:  base = 0, limit = 4 GiB,
                    │                  data, writable, privilege 0
                    ▼
   checks: is 0x1000 within the limit?  may we use this segment?
   address = base + offset = 0 + 0x1000 = 0x1000
```

The address you get after adding the base is called the **linear
address**. Once paging is on (lesson 7), the linear address then goes
through paging to get the physical address. For now, with paging off, the
linear address is the physical address.

### The flat model: making segmentation do nothing

Segmentation can't be switched off in 32-bit mode: every memory access goes
through a segment, always. But it can be made harmless. If every segment
has **base 0 and a limit of 4 GiB**, then `address = 0 + offset` and every
offset is within the limit. The linear address is just the offset, as if
segmentation didn't exist. This is called the **flat model**, and it's
what every modern operating system uses, including ours. Memory protection
will be done by **paging** instead (lesson 7), which is much more flexible.

So if segmentation does nothing, why does this lesson exist?

---

## 3. Why we can't ignore it: privilege levels

There's one thing segments still do that we can't get any other way: they
decide **whether the CPU is running in kernel mode or in user mode**.

The x86 CPU has four **privilege levels**, numbered 0 to 3 and usually
called **rings**. Ring 0 is the most privileged, ring 3 the least. Like
almost every operating system, we use only two of them:

- **Ring 0, kernel mode**: the kernel runs here. Every instruction is
  allowed.
- **Ring 3, user mode**: programs run here. Some instructions are
  forbidden: `hlt` (stop the CPU), `cli` (turn off interrupts), `in` and
  `out` (talk to devices), `lgdt` (change the GDT, as we'll do in this
  lesson), and a few others. In lesson 7 we'll also see that ring 3 can't
  touch memory marked as the kernel's. If a program tries any of this, the
  CPU stops it and hands control to the kernel.

This is what makes an operating system safe: a buggy or hostile program
can't take over the machine, because the CPU itself refuses.

How does the CPU know which ring it's in? **From the code segment.** Every
descriptor has a field called the **DPL** ("descriptor privilege level"),
from 0 to 3. When `CS` holds a code segment whose DPL is 0, the CPU runs in
ring 0. When it holds a code segment whose DPL is 3, it runs in ring 3.
The current ring is called the **CPL** ("current privilege level"), and the
CPU keeps it in the lowest two bits of `CS`.

So to run user programs, we need code and data segments with DPL 3, in
addition to the kernel's segments with DPL 0. Our GDT has these entries:

| Entry | Segment | Base | Limit | DPL | Used for |
|---|---|---|---|---|---|
| 0 | null | | | | Required to be empty (section 5). |
| 1 | kernel code | 0 | 4 GiB | 0 | `CS` while the kernel runs |
| 2 | kernel data | 0 | 4 GiB | 0 | `DS`, `ES`, `FS`, `GS`, `SS` while the kernel runs |
| 3 | user code | 0 | 4 GiB | 3 | `CS` while a user program runs (lesson 10) |
| 4 | user data | 0 | 4 GiB | 3 | the other segment registers in user programs (lesson 10) |

All four real segments are flat: base 0, limit 4 GiB. They differ only in
their type (code or data) and their privilege level. In lesson 10 we'll
add a sixth entry of a different kind, for something called the TSS.

---

## 4. Why the loader's GDT isn't good enough

The bootloader had to create a GDT to switch the CPU to 32-bit mode, and
it's still in use when our kernel starts. Here it is, read with GDB (the
address comes from the `sgdt` instruction, explained in section 9):

```
(gdb) x/5xg 0xcb2b8
0xcb2b8:  0x0000000000000000  0x00cf9a000000ffff
0xcb2c8:  0x00cf93000000ffff  0x00009e000000ffff
0xcb2d8:  0x000092000000ffff
```

Five 8-byte entries. After section 6 you'll be able to decode them
yourself; for now, here's what they are: an empty entry, a flat 32-bit
code segment, a flat 32-bit data segment, and two 16-bit segments that the
loader used while switching modes. That's enough for the kernel to run, but
there are two problems:

1. **It has no user-mode segments.** Every segment has DPL 0. We'll need
   DPL 3 segments in lesson 10.
2. **It lives in memory we don't own.** Address `0xcb2b8` is in the range
   `0xA0000`–`0xEFFFF`, which the memory map from lesson 2 doesn't list at
   all: it's an area the BIOS uses. We have no guarantee about what happens
   to it. The CPU reads the GDT again every time a segment register is
   loaded (and in lesson 4, every time an interrupt happens), so if that
   memory ever changed, the next segment load would crash the machine.

The solution is to build our own GDT, in the kernel's memory, and switch to
it.

---

## 5. Selectors: what a segment register really holds

In protected mode, the value in a segment register is called a
**selector**. It's 16 bits, and it's made of three parts:

```
 15                                3   2   1  0
┌───────────────────────────────────┬────┬──────┐
│            index                  │ TI │ RPL  │
└───────────────────────────────────┴────┴──────┘
```

- **Index** (bits 3–15): which GDT entry to use.
- **TI** (bit 2): which table. 0 means the GDT. (1 means a second, per-task
  table called the LDT, which we never use.)
- **RPL** (bits 0–1): the "requested privilege level", the ring this
  selector claims to be used from. For the kernel's selectors it's 0, for
  user selectors 3. Lesson 10 explains the rules; for now, just know that
  user selectors must have RPL 3.

Since each GDT entry is 8 bytes and the index starts at bit 3, a selector
with its low 3 bits cleared is exactly the **byte offset of the entry**
in the table: index × 8. That makes selectors easy to compute:

| Entry | Index × 8 | TI | RPL | Selector |
|---|---|---|---|---|
| 1, kernel code | `0x08` | 0 | 0 | **`0x08`** |
| 2, kernel data | `0x10` | 0 | 0 | **`0x10`** |
| 3, user code | `0x18` | 0 | 3 | `0x18 | 3` = **`0x1B`** |
| 4, user data | `0x20` | 0 | 3 | `0x20 | 3` = **`0x23`** |

You'll see these four numbers for the rest of the project. When the kernel
reports a crash, `cs=0008` means the kernel was running, and `cs=001b`
means a user program was.

The loader happened to use the same layout for its first three entries,
which is why the milestone output says `cs=8` both before and after.

**Entry 0 must be empty.** The CPU treats selector 0 as the **null
selector**: you're allowed to put it in `DS` or `ES` to mean "no segment",
but any memory access through it causes an error. This makes a bug like
"forgot to set DS" fail loudly instead of silently using a random segment.
Because of this rule, GDT entry 0 is never used, and it's customary to fill
it with zeros.

---

## 6. Descriptors: the 8 bytes of a GDT entry

### The layout, and why it's so strange

Each descriptor is 8 bytes. You'd expect the base to be one 32-bit field
and the limit another. Instead, they're cut into pieces and scattered:

```
byte:    0      1      2      3      4      5        6           7
      ┌──────┬──────┬──────┬──────┬──────┬────────┬───────────┬──────┐
      │ limit 0..15 │ base 0..15  │ base │ access │flags│limit│ base │
      │             │             │16..23│  byte  │     │16-19│24..31│
      └──────┴──────┴──────┴──────┴──────┴────────┴───────────┴──────┘
```

The reason is history. On the 80286 (a 16-bit CPU), a descriptor had a
24-bit base and a 16-bit limit, and used only the first 6 bytes. When the
80386 extended everything to 32 bits, Intel had to keep the old layout
working, so the extra bits went into the two bytes that were left: the
top 4 bits of the limit and the top 8 bits of the base ended up at the end.

In C, the reference describes this with a packed structure:

```c
struct gdt_entry {
    uint16_t limit_low;         /* limit bits 0..15                     */
    uint16_t base_low;          /* base bits 0..15                      */
    uint8_t  base_mid;          /* base bits 16..23                     */
    uint8_t  access;            /* present, DPL, type                   */
    uint8_t  limit_high_flags;  /* limit bits 16..19 | flags << 4       */
    uint8_t  base_high;         /* base bits 24..31                     */
} __attribute__((packed));
```

and fills it with one function that does the cutting, so the rest of the
code never has to think about it:

```c
static void set_entry(int i, uint32_t base, uint32_t limit,
                      uint8_t access, uint8_t flags)
{
    gdt[i].limit_low        = limit & 0xFFFF;
    gdt[i].base_low         = base & 0xFFFF;
    gdt[i].base_mid         = (base >> 16) & 0xFF;
    gdt[i].access           = access;
    gdt[i].limit_high_flags = ((limit >> 16) & 0x0F) | (flags << 4);
    gdt[i].base_high        = (base >> 24) & 0xFF;
}
```

### The limit and the granularity flag

The limit has only 20 bits, so its maximum value is `0xFFFFF`, which is
just under 1 MiB. To describe a 4 GiB segment, there's a flag called **G**
("granularity"). When G is 1, the limit counts in units of 4 KiB instead of
bytes. Then a limit of `0xFFFFF` means `(0xFFFFF + 1) × 4096` bytes = 4 GiB.
(The limit is the offset of the *last* valid unit, which is why we add 1.)

### The flags

The flags are 4 bits, stored in the high half of byte 6:

| Bit | Name | Our value | Meaning |
|---|---|---|---|
| 3 | **G** (granularity) | 1 | The limit is counted in 4 KiB units. |
| 2 | **D** (default size) | 1 | A 32-bit segment. With 0, code would be decoded as 16-bit instructions. |
| 1 | L | 0 | Only used for 64-bit code. |
| 0 | AVL | 0 | Free for the OS to use; we don't. |

`1100` in binary is **`0xC`**. All four of our segments use it.

### The access byte

The access byte (byte 5) holds the most important information:

```
  bit:   7     6  5      4      3     2     1     0
       ┌───┬────────┬──────┬─────┬─────┬─────┬─────┐
       │ P │  DPL   │  S   │  E  │ DC  │ RW  │  A  │
       └───┴────────┴──────┴─────┴─────┴─────┴─────┘
```

- **P** ("present"): 1 means the entry is valid. Loading a segment whose P
  is 0 causes an error.
- **DPL**: the privilege level, 0 (kernel) or 3 (user).
- **S**: 1 for a code or data segment. (0 is for special system entries,
  like the TSS in lesson 10.)
- **E** ("executable"): 1 for a code segment, 0 for a data segment.
- **DC**: for data, the direction the segment grows; for code, whether
  less privileged code may jump into it. 0 for all our segments.
- **RW**: for code, 1 means the code segment can also be *read* (not just
  executed). For data, 1 means it can be *written*. We set it on all four.
- **A** ("accessed"): the CPU sets this bit itself the first time the
  segment is loaded. We start with 0.

Putting it together for our four segments:

```
                 P  DPL  S  E DC RW  A
kernel code:     1  00   1  1  0  1  0   = 1001 1010 = 0x9A
kernel data:     1  00   1  0  0  1  0   = 1001 0010 = 0x92
user code:       1  11   1  1  0  1  0   = 1111 1010 = 0xFA
user data:       1  11   1  0  0  1  0   = 1111 0010 = 0xF2
```

The kernel and user versions differ only in the two DPL bits.

### A worked example: decoding a real entry

Here's our GDT in memory, after `gdt_init()` has run:

```
(gdb) x/5xg &gdt
0x102020 <gdt>:     0x0000000000000000  0x00cf9a000000ffff
0x102030 <gdt+16>:  0x00cf93000000ffff  0x00cffa000000ffff
0x102040 <gdt+32>:  0x00cff2000000ffff
```

GDB prints each entry as one 64-bit number. Take entry 1,
`0x00cf9a000000ffff`. Because x86 is little-endian, the bytes in memory go
from the lowest part of the number to the highest:

```
byte:     0    1    2    3    4    5    6    7
value:   ff   ff   00   00   00   9a   cf   00

limit bits 0..15  = 0xffff        (bytes 0-1)
base bits 0..15   = 0x0000        (bytes 2-3)
base bits 16..23  = 0x00          (byte 4)
access            = 0x9a          (byte 5): kernel code
flags             = 0xc           (byte 6, high half): G=1, D=1
limit bits 16..19 = 0xf           (byte 6, low half)
base bits 24..31  = 0x00          (byte 7)

base = 0x00000000, limit = 0xfffff × 4 KiB → 4 GiB
```

Now look at entry 2, the kernel data segment: its access byte is `0x93`,
not the `0x92` we wrote. That's the **accessed** bit (bit 0): the CPU set
it when we loaded the data segment registers. It's harmless, and a nice
proof that the CPU really reads this table.

You can now decode the loader's GDT from section 4 too. Its entry 3,
`0x00009e000000ffff`, has access byte `0x9e` (a code segment) and flags
`0x0`: G = 0 and D = 0, so it's a 16-bit code segment with a 64 KiB limit.
The loader uses it to switch back to 16-bit mode when it needs to call the
BIOS.

---

## 7. Telling the CPU where the table is

The CPU finds the GDT through a special register called **GDTR**, which
holds the table's address and size. The instruction **`lgdt`** loads it.
It takes, as its operand, the address of a 6-byte structure in memory:

| Offset | Size | Field | Meaning |
|---|---|---|---|
| 0 | 2 bytes | limit | The size of the table **minus 1**. |
| 2 | 4 bytes | base | The address of the table. |

Why "minus 1"? With 16 bits, the largest value is 65535. Storing the size
minus 1 lets a full table of 65536 bytes (8192 entries) be described. For
our 5 entries of 8 bytes, the limit is `5 × 8 - 1 = 39`, which is the
`limit=39` in the milestone output.

Here's the real structure in memory:

```
(gdb) x/6xb &gdtr
0x102000 <gdtr>:  0x27  0x00  0x20  0x20  0x10  0x00
                  └─ limit ─┘  └──────── base ────────┘
                    = 0x27 = 39    = 0x00102020 (the address of gdt)
```

In C:

```c
struct gdt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));
```

**Here `packed` is essential.** Without it, GCC puts 2 bytes of padding
after `limit`, so that `base` starts at offset 4, which suits the CPU
better. The structure becomes 8 bytes instead of 6, and `lgdt`, which
always reads the base from offset 2, gets the padding bytes and half of the
real address. The table would seem to be at some random address, and the
next segment load would crash.

Both the table and this structure must be **global variables** (the
reference makes them `static` globals in `gdt.c`), not local variables of
a function. The CPU doesn't copy the table when you run `lgdt`: it keeps
reading it from memory, whenever a segment register is loaded. A local
variable disappears when the function returns, and its memory gets reused
for something else.

---

## 8. Reloading the segment registers

### The hidden part of a segment register

After `lgdt`, nothing visible has changed. The segment registers still
contain the same selectors, and the CPU still uses the loader's segments.
To understand why, you need to know about a hidden part of segment
registers.

Reading the GDT from memory on every single memory access would be far too
slow. So each segment register has a hidden part, the **descriptor cache**.
When you load a selector into a segment register, the CPU looks up the
entry in the GDT once and copies the base, limit and access rights into
the hidden part. From then on, it uses the cached copy:

```
  DS:   visible part           hidden part (descriptor cache)
       ┌────────────┐        ┌──────────────────────────────────────┐
       │   0x0010   │        │ base 0, limit 4 GiB, data, DPL 0 ... │
       └────────────┘        └──────────────────────────────────────┘
        what you see           filled in from the GDT when DS was loaded
```

`lgdt` only changes GDTR, not the caches. So after `lgdt`, every segment
register still holds a copy of the loader's descriptor, and the CPU still
uses it. To start using our table, we must **load every segment register
again**, so the CPU reads the new entries.

### Data segment registers: a plain `mov`

`DS`, `ES`, `FS`, `GS` and `SS` can be loaded with `mov`. A segment
register can only be loaded from a general-purpose register, not from a
constant, so it takes two instructions:

```nasm
    mov ax, 0x10        ; kernel data selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
```

Changing `SS` in the middle of a function might look dangerous, since it's
the stack segment. It's fine here because the old and new stack segments
both have base 0: `ESP` still points to the same bytes.

### The code segment: a far jump

`CS` can't be loaded with `mov`. The CPU doesn't allow it, because changing
the code segment without also changing the instruction pointer would make
no sense. `CS` changes only together with `EIP`, through a **far jump**, a
far call, a far return, or an interrupt return. A far jump names both the
segment and the address:

```nasm
    jmp 0x08:.reload_cs     ; CS = 0x08, EIP = address of .reload_cs
.reload_cs:
    ret
```

This jumps to the very next instruction, so the program carries on as if
nothing happened, but the CPU reloads `CS` from GDT entry 1 on the way.

### Why this needs a separate assembly file

All of this could be written with GCC's inline assembly, but a far jump to
a label is awkward to express there. It's simpler to write a small
function in NASM, `gdt_load`, that takes a pointer to the `gdt_ptr`
structure and does everything: `lgdt`, the five `mov`s and the far jump.
It's called from C like any other function, so following the calling
convention from lesson 0, its argument is at `[esp + 4]`:

```nasm
gdt_load:
    mov eax, [esp + 4]      ; the argument: address of the gdt_ptr structure
    lgdt [eax]
    ...
```

---

## 9. Writing it

You'll write three new files in `kernel/arch/i386/`, and change `kmain.c`.

### Step 1: `gdt.h`, the selectors

The selectors are what the rest of the kernel needs from this module: the
interrupt code in lesson 4 and the user-mode code in lesson 10 use them.
Define them as constants, and declare `gdt_init()`:

```c
#define SEG_KCODE  0x08         /* GDT[1], ring 0 */
#define SEG_KDATA  0x10         /* GDT[2], ring 0 */
#define SEG_UCODE  (0x18 | 3)   /* GDT[3], ring 3 */
#define SEG_UDATA  (0x20 | 3)   /* GDT[4], ring 3 */

void gdt_init(void);
```

### Step 2: `gdt.c`, the table

- The two packed structures from sections 6 and 7.
- Constants for the four access bytes and the flags, with comments showing
  their bits.
- A `static struct gdt_entry gdt[5];` and a `static struct gdt_ptr gdtr;`.
- `set_entry()`, from section 6.
- `gdt_init()`: fill the five entries (entry 0 all zeros, then the four
  flat segments with base 0 and limit `0xFFFFF`), fill `gdtr`, and call
  `gdt_load(&gdtr)`.

Since `gdt_load` is in an assembly file, `gdt.c` needs to declare it:
`void gdt_load(struct gdt_ptr *p);`.

### Step 3: `gdt_load.asm`

The function from section 8: `lgdt`, reload the five data segment
registers with `0x10`, far jump with `0x08`, `ret`. Mark it `global
gdt_load` so the linker can connect the C call to it. The Makefile picks up
the new `.asm` file automatically.

### Step 4: check it with `sgdt`

How can you tell it worked? The instruction **`sgdt`** does the opposite of
`lgdt`: it stores the current GDTR into 6 bytes of memory. Write a small
function in `kmain.c` that uses it, and also reads `CS`:

```c
static void print_gdtr(const char *label)
{
    struct { uint16_t limit; uint32_t base; } __attribute__((packed)) gdtr;
    uint16_t cs;

    __asm__ volatile ("sgdt %0" : "=m"(gdtr));
    __asm__ volatile ("mov %%cs, %0" : "=r"(cs));
    kprintf("%s: base=%08lx limit=%u cs=%x\n", label, gdtr.base, gdtr.limit, cs);
}
```

Two new inline assembly details: `"=m"(gdtr)` means "the operand is the
memory location of `gdtr`" (instead of a register), and `%%cs` is how you
write a register name in the template. The `%` has to be doubled because
a single `%` introduces an operand like `%0`.

In `kmain`, after printing the boot information:

```c
print_gdtr("loader's GDT");
gdt_init();
print_gdtr("our GDT     ");
```

**Check:** `make run`. The last lines before the panic should be:

```
loader's GDT: base=000cb2b8 limit=39 cs=8
our GDT     : base=00102020 limit=39 cs=8
```

The base of our GDT should be the address of `gdt` in your kernel. Check
with `i686-elf-nm build/kernel.elf | grep gdt`, which for the reference
code prints `00102020 b gdt` (the exact address depends on your code). If
the machine resets instead, go to the exercises: they show how to find
out which instruction failed.

<details>
<summary>Reference: <code>gdt.h</code></summary>

```c
/* gdt.h -- segment selectors and GDT setup. */
#ifndef ARCH_GDT_H
#define ARCH_GDT_H

/* A selector is (index << 3) | RPL. RPL = privilege level we claim (0 or 3). */
#define SEG_KCODE  0x08         /* GDT[1], ring 0 */
#define SEG_KDATA  0x10         /* GDT[2], ring 0 */
#define SEG_UCODE  (0x18 | 3)   /* GDT[3], ring 3 */
#define SEG_UDATA  (0x20 | 3)   /* GDT[4], ring 3 */

#ifndef __ASSEMBLER__
void gdt_init(void);
#endif

#endif
```

The `#ifndef __ASSEMBLER__` lets assembly files that go through the C
preprocessor include this header and get just the constants. Our NASM
files don't do that (they define their own copies with `equ`), so you can
leave it out.

</details>

<details>
<summary>Reference: <code>gdt.c</code></summary>

```c
/* gdt.c -- our own Global Descriptor Table.
 *
 * We use "flat" segments: every segment starts at 0 and covers 4 GiB, so
 * segmentation does nothing to addresses. Segments still matter for ONE
 * thing: the privilege level. Code running with CS = a DPL-3 segment is in
 * user mode (ring 3); CS = a DPL-0 segment is kernel mode (ring 0).
 */
#include <stdint.h>
#include "arch/i386/gdt.h"

struct gdt_entry {
    uint16_t limit_low;         /* limit bits 0..15                     */
    uint16_t base_low;          /* base bits 0..15                      */
    uint8_t  base_mid;          /* base bits 16..23                     */
    uint8_t  access;            /* present, DPL, type (see table below) */
    uint8_t  limit_high_flags;  /* limit bits 16..19 | flags << 4       */
    uint8_t  base_high;         /* base bits 24..31                     */
} __attribute__((packed));

/* The operand of LGDT: size-1 and linear address of the table. */
struct gdt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

/* Access byte: P=1 | DPL (2 bits) | S=1 (code/data) | type
 *   0x9A = 1001 1010: present, ring 0, code, readable
 *   0x92 = 1001 0010: present, ring 0, data, writable
 *   0xFA = 1111 1010: present, ring 3, code, readable
 *   0xF2 = 1111 0010: present, ring 3, data, writable */
#define ACCESS_KCODE 0x9A
#define ACCESS_KDATA 0x92
#define ACCESS_UCODE 0xFA
#define ACCESS_UDATA 0xF2

/* Flags nibble: G=1 (limit counts 4 KiB pages), D=1 (32-bit segment) */
#define FLAGS_32BIT_4K 0xC

static struct gdt_entry gdt[5];
static struct gdt_ptr   gdtr;

void gdt_load(struct gdt_ptr *p);  /* gdt_load.asm */

static void set_entry(int i, uint32_t base, uint32_t limit,
                      uint8_t access, uint8_t flags)
{
    gdt[i].limit_low        = limit & 0xFFFF;
    gdt[i].base_low         = base & 0xFFFF;
    gdt[i].base_mid         = (base >> 16) & 0xFF;
    gdt[i].access           = access;
    gdt[i].limit_high_flags = ((limit >> 16) & 0x0F) | (flags << 4);
    gdt[i].base_high        = (base >> 24) & 0xFF;
}

void gdt_init(void)
{
    set_entry(0, 0, 0, 0, 0);                                   /* null */
    set_entry(1, 0, 0xFFFFF, ACCESS_KCODE, FLAGS_32BIT_4K);     /* 0x08 */
    set_entry(2, 0, 0xFFFFF, ACCESS_KDATA, FLAGS_32BIT_4K);     /* 0x10 */
    set_entry(3, 0, 0xFFFFF, ACCESS_UCODE, FLAGS_32BIT_4K);     /* 0x18 */
    set_entry(4, 0, 0xFFFFF, ACCESS_UDATA, FLAGS_32BIT_4K);     /* 0x20 */

    gdtr.limit = sizeof(gdt) - 1;
    gdtr.base  = (uint32_t)gdt;
    gdt_load(&gdtr);
}
```

</details>

<details>
<summary>Reference: <code>gdt_load.asm</code></summary>

```nasm
; gdt_load.asm -- load a new GDT and make every segment register use it.
;
; The CPU caches the descriptor of each segment register when it is loaded.
; LGDT alone changes nothing visible: we must reload all six registers.

global gdt_load

SEG_KCODE equ 0x08
SEG_KDATA equ 0x10

; void gdt_load(struct gdt_ptr *p)
gdt_load:
    mov eax, [esp + 4]
    lgdt [eax]

    mov ax, SEG_KDATA           ; data segments: a plain MOV works
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    jmp SEG_KCODE:.reload_cs    ; CS can only be changed by a far jump/call/ret
.reload_cs:
    ret
```

</details>

---

## 10. Exercises: breaking it on purpose

Mistakes in the GDT don't print anything: the machine just stops. These
exercises show you what such a failure looks like, so you can recognize and
locate it later. For each one, enable QEMU's interrupt log by uncommenting
this line in the Makefile:

```make
QEMUFLAGS += -d int,cpu_reset -D $(BUILD)/qemu.log
```

or pass it for one run with
`make run QEMUEXTRA="-d int,cpu_reset -D build/qemu.log"`.

1. **A missing data segment.** Set `ACCESS_KDATA` to `0x00` (its P bit is
   now 0: "not present"). Run. The kernel prints the loader's GDT and then
   stops. In `build/qemu.log`, find the first line with `v=`:

   ```
   check_exception old: 0xffffffff new 0xd
        0: v=0d e=0010 i=0 cpl=0 IP=0008:00100edb pc=00100edb ...
   ```

   `v=0d` is exception 13, **general protection**: the CPU's catch-all
   error for "you're not allowed to do that". `e=0010` is the **error
   code**, and for segment errors it's the selector that caused the
   problem: `0x10`, our kernel data selector. `pc=00100edb` is the
   instruction. Find it in the disassembly:

   ```
   $ i686-elf-objdump -d -M intel build/kernel.elf | grep 100edb
     100edb:   8e d8      mov    ds,eax
   ```

   It's the first `mov ds`, right after the `lgdt`: the CPU looked up entry
   2, found it wasn't present, and refused. Since there's no handler for
   exceptions yet (that's lesson 4), the CPU then gives up with a double
   fault (`v=08`) and a triple fault, and the machine resets.

   Look further down the same log entry, at the lines starting with `DS`
   and `GDT`:

   ```
   DS =0010 00000000 ffffffff 00cf9300 DPL=0 DS   [-WA]
   GDT=     00102020 00000027
   ```

   The GDTR already points to our table (`00102020`, limit `0x27`), but DS
   still shows the old, valid descriptor (base 0, limit `ffffffff`). That's
   the descriptor cache from section 8, caught in the act.

2. **A table that's too short.** Put `ACCESS_KDATA` back, and set
   `gdtr.limit = 8 - 1;` (only entry 0 fits). Where does it fail now, and
   with which error code? (The same `mov ds`, with `e=0010`: the selector
   points past the end of the table.)

3. **A code segment that isn't code.** Undo that, and set `ACCESS_KCODE` to
   `0x92` (a data segment). Now the five `mov`s succeed, and the CPU fails
   at the far jump, at `pc=00100ee5`, with `e=0008`: you can't run code from
   a data segment.

4. **Forget `packed`.** Remove `__attribute__((packed))` from
   `struct gdt_ptr` and print `sizeof(struct gdt_ptr)`. What does `lgdt`
   load as the base now? Use the GDB command `x/8xb &gdtr` to see the
   bytes, and work it out before looking at the answer.

   <details>
   <summary>Answer</summary>

   The structure is now 8 bytes: `27 00`, then 2 bytes of padding
   (`00 00`), then the base `20 20 10 00`. `lgdt` reads the base from
   bytes 2 to 5, which are `00 00 20 20`, so it thinks the table is at
   `0x20200000`, far beyond our 128 MiB of RAM. The QEMU log confirms it,
   and shows the first `mov ds` failing:

   ```
        0: v=0d e=0010 i=0 cpl=0 IP=0008:00100edb pc=00100edb ...
   GDT=     20200000 00000027
   ```

   </details>

## Pitfalls

- **Limit = size instead of size − 1.** The CPU then thinks there's one
  more byte in the table than there is. It usually goes unnoticed, which
  is exactly why it's worth getting right.
- **Forgetting `packed` on `struct gdt_ptr`.** `lgdt` reads a garbage base.
- **Forgetting to reload `CS`.** Everything seems fine, because the cached
  copy of the loader's code segment still works. That hides any mistake in
  your code segment entry until something else reloads `CS` from the
  table, such as the first interrupt in lesson 4. The crash then happens
  far from the real bug.
- **Putting the GDT in a local variable.** The CPU keeps reading it after
  the function has returned and its memory has been reused.
- **Using the wrong selector.** `0x10` in `CS` or `0x08` in `DS` fails, and
  so does any selector whose index is past the limit.

## References

- Intel® 64 and IA-32 Architectures Software Developer's Manual, Volume 3A:
  section 3.4.2 (segment selectors), 3.4.3 (segment registers and their
  hidden part), 3.4.5 (segment descriptors), 5.5 (privilege levels).
- OSDev wiki: "Segmentation", "GDT", "GDT Tutorial".
- xv6: `seginit()` in `vm.c` and the `SEG` macros in `mmu.h`, the same
  table built a different way.
