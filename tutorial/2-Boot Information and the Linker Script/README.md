# Lesson 2 — Boot Information and the Linker Script

In this lesson the kernel finds out about the machine it's running on. How
much memory is there? Which parts of it can we use? Where in memory is the
kernel itself? It answers these questions by reading what the bootloader
left for it, and by asking the linker where it put things.

When you're done, the kernel prints this (the command line is set with
`make run QEMUEXTRA="-append hello"`, explained in section 3):

```
toyos booting...
boot info at 0x00009500, memory map at 0x00009000
loader:  qemu
cmdline: "build/kernel.elf hello"
memory:  639 KiB low, 129920 KiB high
memory map:
  00000000 - 0009fbff  available
  0009fc00 - 0009ffff  reserved
  000f0000 - 000fffff  reserved
  00100000 - 07fdffff  available
  07fe0000 - 07ffffff  reserved
  fffc0000 - ffffffff  reserved
kernel:  0x00100000 - 0x00106010 (24 KiB)

PANIC: nothing more to do
```

Nothing here changes how the kernel behaves yet. But in lesson 6 the
kernel starts handing out memory, and to do that it must know exactly which
bytes of RAM are free. Everything this lesson prints is what lesson 6 will
be built on.

The reference code is in [`code/`](code/). To see every change since
lesson 1:

```sh
cd tutorial
diff -ru "1-Console Output/code" "2-Boot Information and the Linker Script/code"
```

**Contents**

1. [Why the kernel needs to know about memory](#1-why-the-kernel-needs-to-know-about-memory)
2. [The Multiboot information structure](#2-the-multiboot-information-structure)
3. [Strings from the loader: the command line and the loader's name](#3-strings-from-the-loader-the-command-line-and-the-loaders-name)
4. [The memory map](#4-the-memory-map)
5. [Where is the kernel? Symbols from the linker script](#5-where-is-the-kernel-symbols-from-the-linker-script)
6. [The whole picture](#6-the-whole-picture)
7. [Writing it](#7-writing-it)
8. [Exercises](#8-exercises)

---

## 1. Why the kernel needs to know about memory

An operating system is in charge of all the memory in the machine. When a
program starts, the kernel gives it memory for its code and data. When the
kernel needs room for a new data structure, it takes some memory for
itself. To do this, it must keep track of which parts of memory are free
and which are in use.

To start that bookkeeping, the kernel needs to know three things:

1. **Which memory exists.** We start QEMU with 128 MiB, but a real PC
   might have 4 GiB or 64 MiB, and the kernel can't assume anything.
2. **Which parts of it are usable.** Not every address is ordinary RAM. In
   lesson 0 you saw that the first megabyte contains the screen's memory
   and the BIOS's code, and there are other areas like that higher up. If
   the kernel stores data there, it either doesn't stick (it's not RAM) or
   it destroys something the hardware needs.
3. **Which usable parts are already taken.** The kernel's own code and
   data are somewhere in RAM, and so is the information the bootloader
   left us. The kernel must not hand those bytes out to anyone else.

The first two come from the bootloader, which got them from the BIOS. The
third comes partly from the bootloader and partly from the linker.

Why ask the bootloader? Finding out which memory exists is surprisingly
hard. There's no CPU instruction for it. The usual method is to ask the
BIOS, but the BIOS can only be called from the 16-bit mode the CPU starts
in, and our kernel runs in 32-bit mode. The bootloader runs before the
switch to 32-bit mode, so it asks the BIOS on our behalf and writes down
the answer. That's one of the services the Multiboot standard provides, and
we asked for it in lesson 0 by setting bit 1 in the header's `flags`.

---

## 2. The Multiboot information structure

### Where it is

In lesson 0 you saw that when `_start` runs, register `EBX` holds the
address of the **Multiboot information structure**, and that `_start`
passes it to `kmain` as its second argument, `mbi_phys`. In QEMU, its
value is `0x9500`.

The Multiboot information structure is a block of memory that the loader
filled in before jumping to our kernel. It's a C-style structure: a series
of fields, each at a fixed offset from the start. The Multiboot
specification (section 3.3) defines the layout. Here are the fields we use:

| Offset | Field | What it contains |
|---|---|---|
| 0 | `flags` | Which of the other fields are valid (explained below). |
| 4 | `mem_lower` | The amount of memory below 1 MiB, in KiB. |
| 8 | `mem_upper` | The amount of memory starting at 1 MiB, up to the first hole, in KiB. |
| 16 | `cmdline` | The address of the kernel's command line, a C string. |
| 20 | `mods_count` | The number of extra files the loader loaded with the kernel (lesson 12). |
| 24 | `mods_addr` | The address of the list of those files. |
| 44 | `mmap_length` | The size of the memory map, in bytes. |
| 48 | `mmap_addr` | The address of the memory map (section 4). |
| 64 | `boot_loader_name` | The address of the loader's name, a C string. |

The other offsets hold fields we don't need, like `boot_device` (which disk
the BIOS booted from) at offset 12.

### The flags field: which fields are valid

Not every loader fills in every field. An old loader might not know about
the memory map, and a loader that wasn't given a command line has nothing
to put in `cmdline`. So the first field, `flags`, has one bit per group of
fields, and **a field is only valid if its bit is set**. If the bit is 0,
the field may contain anything, and reading it would give you garbage.

| Bit | Fields it validates |
|---|---|
| 0 | `mem_lower`, `mem_upper` |
| 2 | `cmdline` |
| 3 | `mods_count`, `mods_addr` |
| 6 | `mmap_length`, `mmap_addr` |
| 9 | `boot_loader_name` |

**Always check the bit before reading the field.** For example, to print
the loader's name:

```c
if (mbi->flags & (1 << 9))
    kprintf("loader:  %s\n", (char *)mbi->boot_loader_name);
```

### What QEMU actually gives us

Here are the first 20 words (4 bytes each) at `0x9500`, read with GDB when
the kernel was started with `-append hello`:

```
(gdb) x/20xw 0x9500
0x9500:  0x0000024f  0x0000027f  0x0001fb80  0x8000ffff
0x9510:  0x00107000  0x00000000  0x00107000  0x00000000
0x9520:  0x00000000  0x00000000  0x00000000  0x00000090
0x9530:  0x00009000  0x00000000  0x00000000  0x00000000
0x9540:  0x00107017  0x00000000  0x00000000  0x00000000
```

Let's decode it, one field at a time:

- **Offset 0, `flags` = `0x24f`.** In binary that's `10 0100 1111`: bits 0,
  1, 2, 3, 6 and 9 are set. So the memory sizes, the command line, the
  modules, the memory map and the loader name are all valid. (Bit 1 is
  `boot_device`, which we don't use.)
- **Offset 4, `mem_lower` = `0x27f`** = 639 KiB of memory below 1 MiB.
- **Offset 8, `mem_upper` = `0x1fb80`** = 129920 KiB of memory from 1 MiB
  up to the first hole. That's 126.875 MiB. We'll see in section 4 why it's
  not the full 127 MiB.
- **Offset 16, `cmdline` = `0x107000`**: the command line is at that address.
- **Offset 20, `mods_count` = 0**: no modules. (`mods_addr`, at offset 24,
  is `0x107000`, but since the count is 0 there's nothing there.)
- **Offset 44, `mmap_length` = `0x90`** = 144 bytes of memory map.
- **Offset 48, `mmap_addr` = `0x9000`**: the memory map starts there.
- **Offset 64, `boot_loader_name` = `0x107017`**: the loader's name is
  there.

### Addresses are numbers, not pointers

Notice that the addresses in this structure (`cmdline`, `mmap_addr`,
`boot_loader_name`) are **physical addresses**: real locations in RAM.
Right now, with paging off, a physical address is also what a C pointer
contains, so we can simply cast:

```c
char *name = (char *)mbi->boot_loader_name;
```

That will stop being true in lesson 8, when the kernel moves to a
different address range and pointers no longer equal physical addresses.
So in the C structure, we declare these fields as **`uint32_t`, not as
pointers**. A `uint32_t` can't be dereferenced by accident: you have to
write a cast, and each cast is a place you'll revisit in lesson 8.

### Writing the structure in C

Create `kernel/arch/i386/multiboot.h`. The structure must have every field
up to `boot_loader_name`, in the right order and size, even the ones we
don't use, because the offsets have to match the specification. Fields we
don't care about can get any name (the reference calls the four words at
offsets 28 to 40 `syms[4]`):

```c
struct multiboot_info {
    uint32_t flags;             /* offset 0  */
    uint32_t mem_lower;         /* offset 4  */
    uint32_t mem_upper;         /* offset 8  */
    uint32_t boot_device;       /* offset 12 */
    uint32_t cmdline;           /* offset 16 */
    ...
```

Then give the flag bits names, so the code says `MB_INFO_MEM_MAP` instead
of `1 << 6`. Also move `MULTIBOOT_BOOTLOADER_MAGIC` (`0x2BADB002`) from
`kmain.c` into this header, since it's part of the same standard.

A good way to check that the offsets are right is to compare with the
GDB dump above: `mmap_addr` must be at offset 48 (`0x9530`) and contain
`0x9000`.

---

## 3. Strings from the loader: the command line and the loader's name

The **command line** is a string the loader passes to the kernel, like the
arguments of a normal program. Real kernels use it for options: Linux, for
example, accepts things like `root=/dev/sda1` to say which disk holds the
files. Our kernel doesn't need options yet, but it's useful to know it's
there.

QEMU builds the command line from the kernel's file name, then whatever you
pass with its **`-append`** option. To pass extra options to QEMU through
`make`, the reference Makefile adds a variable, **`QEMUEXTRA`**, to every
QEMU command:

```make
run: $(KERNEL)
	$(QEMU) $(QEMUFLAGS) $(QEMUEXTRA) -kernel $(KERNEL)
```

Make the same change in the `run-nox` and `debug` targets (`run-test`
already has it). Now `make run QEMUEXTRA="-append hello"` runs QEMU with
`-append hello`, and the command line becomes `"build/kernel.elf hello"`.
You can use `QEMUEXTRA` for any other QEMU option too, like `-m 512M`.

The **loader's name** for QEMU's built-in loader is simply `"qemu"`. GRUB
would say something like `"GNU GRUB 0.97"`.

Both strings are at addresses just past `0x107000`:

```
(gdb) x/s 0x107000
0x107000:  "build/kernel.elf hello"
(gdb) x/s 0x107017
0x107017:  "qemu"
```

**Remember this address.** Our kernel ends at `0x106010` (section 5), and
the loader put these strings at `0x107000`, right after it. That's typical:
loaders put their data in memory that looks free to them. When lesson 6
starts handing out memory above the kernel, it must not hand out these
bytes until we've finished reading them.

---

## 4. The memory map

### Why the memory sizes aren't enough

`mem_lower` and `mem_upper` give two sizes: memory below 1 MiB, and memory
from 1 MiB up to the first hole. But physical memory on a PC isn't one
continuous block of RAM. It has **holes**: address ranges that aren't RAM,
or that are RAM the firmware has reserved for itself. `mem_upper` stops at
the first hole and says nothing about what comes after it. On a machine
with 4 GiB, that could leave most of the memory unknown.

### What a memory map is

The **memory map** is the complete answer: a list of address ranges, each
marked with its type. Type 1 means "available": ordinary RAM that the
operating system may use. Any other type means "don't touch". (Other types
exist for specific purposes, like memory holding tables the firmware
published for the OS. For us, anything other than 1 is simply "reserved".)

The map is an array of entries starting at `mmap_addr`, with
`mmap_length` bytes in total. Each entry looks like this:

| Offset | Size | Field | Meaning |
|---|---|---|---|
| 0 | 4 | `size` | The size of the rest of this entry, in bytes (normally 20). |
| 4 | 8 | `addr` | The first address of the range. |
| 12 | 8 | `len` | The length of the range, in bytes. |
| 20 | 4 | `type` | 1 = available RAM, anything else = reserved. |

`addr` and `len` are 64-bit numbers. Some 32-bit PCs can have memory
above 4 GiB (using a CPU feature called PAE that we won't use), and the
map must be able to describe it.

Here's the start of the real map at `0x9000`, byte by byte:

```
(gdb) x/36xb 0x9000
0x9000:  14 00 00 00   00 00 00 00 00 00 00 00   00 fc 09 00 00 00 00 00
         └── size ──┘  └──────── addr ────────┘  └──────── len ─────────┘
0x9014:  01 00 00 00
         └── type ──┘
0x9018:  14 00 00 00   00 fc 09 00 00 00 00 00   ...
         └── size ──┘  └──────── addr ────────┘     (the second entry)
```

Read with little-endian byte order: `size` is `0x14` = 20, `addr` is 0,
`len` is `0x9fc00`, and `type` is 1. So the first entry says "from address 0,
`0x9fc00` bytes (639 KiB) of available RAM". That's where `mem_lower`'s 639
came from.

### Walking the map: use the size field

The `size` field has an unusual definition: it's the size of the entry
**not counting the `size` field itself**. So each entry takes `size + 4`
bytes, and the next entry starts `size + 4` bytes after the current one.
In the dump above, the first entry is at `0x9000`, its `size` is 20, and
the second entry starts at `0x9000 + 20 + 4 = 0x9018`.

Why not just use an array index (`entries[i]`)? Because the specification
allows entries to be **larger** than the 24 bytes we know about: a future
loader could add fields at the end. Code that walks the list with `size`
keeps working; code that assumes 24 bytes would get lost. So the loop
looks like this:

```c
uint32_t addr = mbi->mmap_addr;
while (addr < mbi->mmap_addr + mbi->mmap_length) {
    struct multiboot_mmap_entry *e = (struct multiboot_mmap_entry *)addr;
    /* ... use e->addr, e->len, e->type ... */
    addr += e->size + sizeof(e->size);
}
```

`addr` is kept as a number (`uint32_t`), not a pointer, so that adding
`size + 4` moves forward by that many bytes. If it were a
`struct multiboot_mmap_entry *`, adding to it would move by whole structure
sizes, which is exactly what we're trying to avoid.

### The entry as a C structure, and `packed`

```c
struct multiboot_mmap_entry {
    uint32_t size;
    uint64_t addr;
    uint64_t len;
    uint32_t type;
} __attribute__((packed));
```

The `packed` attribute tells GCC not to insert any **padding** between the
fields. Normally a compiler may leave unused bytes between fields so that
each one starts at an address that suits the CPU (an 8-byte number at an
address that's a multiple of 8, for example). That's fine for structures
your own program defines, but this layout is fixed by the specification:
`addr` *must* be at offset 4.

On i386, GCC happens to place 64-bit fields on 4-byte boundaries, so this
particular structure has no padding even without `packed` (you can check:
`offsetof(struct multiboot_mmap_entry, addr)` is 4 either way). But the
same code compiled for a 64-bit CPU would put `addr` at offset 8. Writing
`packed` on every structure whose layout comes from outside the program
(hardware tables, file formats, specifications) is a good habit: it states
that the layout is fixed, and it removes a whole class of surprises. In
the next lesson, you'll meet a structure where forgetting it really does
break things.

### Printing 64-bit numbers

Our `kprintf` only handles 32-bit numbers. On our machine, everything in
the memory map is below 4 GiB, so we can print the low 32 bits by casting:
`(uint32_t)e->addr`.

It's worth understanding what happens if you forget the cast. Lesson 1
showed that `kprintf` reads its arguments from the stack, one after the
other, and that only the format string says how big each one is. A
`uint64_t` takes **8 bytes** on the stack. If the format says `%lx`,
`kprintf` reads only 4 of them, and then reads the other 4 as if they were
the *next* argument. Every value after that is shifted by one. GCC's
format checker (lesson 1) warns about this, which is one more reason to
keep it on.

### Reading the real map

Here's the map QEMU gives a machine with 128 MiB, with an explanation of
each line:

```
00000000 - 0009fbff  available   639 KiB of RAM below the old 640 KiB limit
0009fc00 - 0009ffff  reserved    the "extended BIOS data area", used by the BIOS
000f0000 - 000fffff  reserved    the BIOS's own code (ROM)
00100000 - 07fdffff  available   about 127 MiB of RAM, starting at 1 MiB
07fe0000 - 07ffffff  reserved    tables that the BIOS publishes for the OS (ACPI)
fffc0000 - ffffffff  reserved    the BIOS ROM again, visible at the top of 4 GiB
```

A few things worth noticing:

- **`0xA0000` to `0xEFFFF` isn't listed at all.** That range contains the
  video memory (the text screen at `0xB8000` is in there) and other ROMs.
  It isn't RAM, so the map doesn't mention it. A range that's missing from
  the map is just as forbidden as one marked reserved.
- **The last 128 KiB of RAM are reserved.** The BIOS keeps tables there
  that describe the hardware (they're called ACPI tables). That's why
  `mem_upper` said 129920 KiB, not 130048: the usable range ends at
  `0x07fdffff`, not at the 128 MiB mark (`0x07ffffff`).
- **The map ends with something at the very top of the 4 GiB range**,
  far beyond our 128 MiB. Not every address is RAM: the motherboard maps
  the BIOS chip there, because that's where the CPU fetches its very first
  instruction when it powers on.

The kernel will follow a simple rule from lesson 6 on: **only use
"available" memory, and only above 1 MiB.** The low 640 KiB is technically
usable, but it's small, it's where the loader put its information (the
Multiboot structure at `0x9500` and the map at `0x9000`), and it's not
worth the risk.

---

## 5. Where is the kernel? Symbols from the linker script

The memory map says that everything from 1 MiB up is available. But our
kernel is *in* that range, so part of it isn't really free. We need to know
exactly where the kernel starts and ends.

### What a symbol is

When the linker builds `kernel.elf`, it keeps a list of **symbols**: names,
each with an address. Every function and global variable has one. You saw
this list in lesson 0 with `i686-elf-nm`. For a C global variable such as
`int counter;`, the symbol `counter` is the address of the 4 bytes that
hold the variable.

In lesson 0 we also defined a symbol in the linker script:
`kernel_end = .;`. That symbol is different: it's **just an address**, with
no bytes reserved behind it. It marks a position, the first address after
the kernel. Now add a matching one at the start, right after `. = 1M;`:

```
SECTIONS
{
    . = 1M;
    kernel_start = .;
    ...
    kernel_end = .;
}
```

### Using them from C

To use these symbols from C, you have to declare them. The natural thing to
write is wrong:

```c
extern uint32_t kernel_end;          /* WRONG */
kprintf("%lx\n", kernel_end);
```

This tells C that `kernel_end` is a variable, a `uint32_t` stored *at* the
address `kernel_end`. So `kprintf` receives the 4 bytes found in memory at
`0x106010`, which are just whatever happens to be there, not the address.
You'd have to write `&kernel_end` to get the address. That's easy to forget,
and the mistake compiles silently.

The usual solution is to declare such symbols as **arrays**:

```c
extern char kernel_start[], kernel_end[];
```

In C, the name of an array used in an expression gives its address. So
`kernel_end` is now the address, which is what we want, and there's no
way to accidentally read "the value of `kernel_end`". Because the element
type is `char`, subtracting them gives a size in bytes:
`kernel_end - kernel_start` is the size of the kernel.

### The result

With those symbols, the kernel prints:

```
kernel:  0x00100000 - 0x00106010 (24 KiB)
```

Where do 24 KiB come from, when the kernel file is much smaller? Look at
the sections:

```
$ i686-elf-objdump -h build/kernel.elf
  0 .text    00000e10  00100000  ...
  1 .rodata  000001dc  00101000  ...
  2 .bss     00004010  00102000  ...
```

`.text` (3.5 KiB) is at `0x100000`, `.rodata` (0.5 KiB) is on the next 4
KiB boundary at `0x101000`, and `.bss` is at `0x102000`. The `.bss` is 16
KiB plus 16 bytes. The 16 bytes are the VGA driver's two `static int`
variables, `col` at `0x102000` and `row` at `0x102004`, followed by 8 bytes
of padding, because the stack is aligned on 16 bytes (`align 16` in
`boot.asm`). Then comes the 16 KiB boot stack, from `0x102010` to
`0x106010`.
So the kernel ends at `0x102000 + 0x4010 = 0x106010`. The 16 KiB stack is
two thirds of the kernel's memory, but it takes no space in the file
(lesson 0, section 8).

---

## 6. The whole picture

Putting everything together, here's what the first 1.1 MiB of physical
memory looks like when `kmain` runs:

```
0x00000000 ┌──────────────────────────────────────┐
           │ BIOS data (interrupt table etc.)     │
0x00009000 ├──────────────────────────────────────┤ ◄─ memory map (144 bytes)
0x00009500 ├──────────────────────────────────────┤ ◄─ Multiboot information
           │ ... rest of low memory               │
0x0009FC00 ├──────────────────────────────────────┤
           │ BIOS area, video memory, ROMs        │   not usable
0x00100000 ├──────────────────────────────────────┤ ◄─ kernel_start
           │ .text   code                         │
0x00101000 │ .rodata constants and strings        │
0x00102000 │ .bss    16 KiB boot stack, globals   │
0x00106010 ├──────────────────────────────────────┤ ◄─ kernel_end
           │ (free)                               │
0x00107000 ├──────────────────────────────────────┤ ◄─ command line, loader name
           │ free RAM, up to 0x07FDFFFF           │
           ▼                                      ▼
```

Lesson 6's memory manager starts from this picture: the available ranges
from the memory map, minus everything below 1 MiB, minus the kernel, minus
whatever the loader put there that we still need.

---

## 7. Writing it

Do it in this order, running the kernel after each step.

1. **`multiboot.h`.** The `multiboot_info` structure with all the fields up
   to `boot_loader_name`, the flag bit names, `MULTIBOOT_BOOTLOADER_MAGIC`,
   the memory map entry structure, and a name for "available"
   (`MB_MEMORY_AVAILABLE`, value 1). For lesson 12 you'll also want the
   module structure: four `uint32_t` fields, `mod_start`, `mod_end`,
   `cmdline` and a reserved one. Include it in `kmain.c`.

2. **Print the simple fields.** Write a function
   `print_boot_info(struct multiboot_info *mbi)` in `kmain.c`. In `kmain`,
   call it with `(struct multiboot_info *)mbi_phys`. Print the address of
   the structure, then, each one only if its flag is set, the loader name,
   the command line and the two memory sizes. **Check:** run
   `make run QEMUEXTRA="-append hello"` and compare with the values in
   section 2.

3. **Print the memory map.** If the memory map flag isn't set, `panic`: the
   kernel can't manage memory without it. Otherwise walk the map as shown
   in section 4, printing each range's first and last address and whether
   it's available. The last address is `addr + len - 1`. **Check:** six
   lines, as in section 4.

4. **The kernel's own range.** Add `kernel_start` to the linker script,
   declare both symbols in `kmain.c`, and print them with the size in KiB.
   **Check:** `0x00100000 - 0x00106010`, or close to it if your code is a
   different size. Compare with `i686-elf-nm -n build/kernel.elf | grep
   kernel_`.

5. **Finish** `kmain` with `panic("nothing more to do")`, and remove the
   `kprintf` tests from lesson 1.

<details>
<summary>Reference: <code>arch/i386/multiboot.h</code></summary>

```c
/* multiboot.h -- the information a Multiboot (v1) loader hands to the kernel.
 * Spec: https://www.gnu.org/software/grub/manual/multiboot/multiboot.html
 *
 * All addresses inside these structs are PHYSICAL addresses.
 */
#ifndef ARCH_MULTIBOOT_H
#define ARCH_MULTIBOOT_H

#include <stdint.h>

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002

/* Bits in multiboot_info.flags: which fields below are valid */
#define MB_INFO_MEMORY      (1 << 0)    /* mem_lower, mem_upper */
#define MB_INFO_CMDLINE     (1 << 2)    /* cmdline              */
#define MB_INFO_MODS        (1 << 3)    /* mods_count, mods_addr */
#define MB_INFO_MEM_MAP     (1 << 6)    /* mmap_length, mmap_addr */
#define MB_INFO_LOADER_NAME (1 << 9)    /* boot_loader_name     */

struct multiboot_info {
    uint32_t flags;
    uint32_t mem_lower;         /* KiB of memory below 1 MiB          */
    uint32_t mem_upper;         /* KiB of memory above 1 MiB          */
    uint32_t boot_device;
    uint32_t cmdline;           /* C string                           */
    uint32_t mods_count;        /* boot modules (lesson 12)           */
    uint32_t mods_addr;         /* array of struct multiboot_module   */
    uint32_t syms[4];
    uint32_t mmap_length;       /* size in bytes of the memory map    */
    uint32_t mmap_addr;         /* first struct multiboot_mmap_entry  */
    uint32_t drives_length;
    uint32_t drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;  /* C string                           */
};

#define MB_MEMORY_AVAILABLE 1   /* the only type we may use as RAM */

/* Entries have variable size: the next one starts at
 * (char *)entry + entry->size + sizeof(entry->size). */
struct multiboot_mmap_entry {
    uint32_t size;              /* size of the rest of this entry */
    uint64_t addr;
    uint64_t len;
    uint32_t type;
} __attribute__((packed));

struct multiboot_module {
    uint32_t mod_start;         /* first byte                  */
    uint32_t mod_end;           /* one past the last byte      */
    uint32_t cmdline;           /* C string, e.g. "init"       */
    uint32_t reserved;
};

#endif
```

</details>

<details>
<summary>Reference: <code>kmain.c</code></summary>

```c
/* kmain.c -- the kernel's C entry point, called from boot.asm. */
#include <stdint.h>
#include "arch/i386/multiboot.h"
#include "dev/console.h"
#include "lib/kprintf.h"
#include "lib/panic.h"

/* Defined by linker.ld. Only their ADDRESSES mean something. */
extern char kernel_start[], kernel_end[];

static void print_boot_info(struct multiboot_info *mbi)
{
    kprintf("boot info at %p, memory map at %p\n", mbi, (void *)mbi->mmap_addr);
    if (mbi->flags & MB_INFO_LOADER_NAME)
        kprintf("loader:  %s\n", (char *)mbi->boot_loader_name);
    if (mbi->flags & MB_INFO_CMDLINE)
        kprintf("cmdline: \"%s\"\n", (char *)mbi->cmdline);
    if (mbi->flags & MB_INFO_MEMORY)
        kprintf("memory:  %lu KiB low, %lu KiB high\n", mbi->mem_lower, mbi->mem_upper);

    if (!(mbi->flags & MB_INFO_MEM_MAP))
        panic("loader gave no memory map");

    kprintf("memory map:\n");
    uint32_t addr = mbi->mmap_addr;
    while (addr < mbi->mmap_addr + mbi->mmap_length) {
        struct multiboot_mmap_entry *e = (struct multiboot_mmap_entry *)addr;
        /* Everything fits below 4 GiB on our machine, so print the low 32 bits. */
        kprintf("  %08lx - %08lx  %s\n",
                (uint32_t)e->addr, (uint32_t)(e->addr + e->len - 1),
                e->type == MB_MEMORY_AVAILABLE ? "available" : "reserved");
        addr += e->size + sizeof(e->size);
    }

    kprintf("kernel:  %p - %p (%lu KiB)\n", kernel_start, kernel_end,
            (uint32_t)(kernel_end - kernel_start) / 1024);
}

void kmain(uint32_t magic, uint32_t mbi_phys)
{
    console_init();
    kprintf("toyos booting...\n");

    if (magic != MULTIBOOT_BOOTLOADER_MAGIC)
        panic("not booted by a Multiboot loader (magic=%lx)", magic);

    /* Paging is off: a physical address can be used directly as a pointer. */
    print_boot_info((struct multiboot_info *)mbi_phys);

    panic("nothing more to do");
}
```

</details>

---

## 8. Exercises

1. **Different memory sizes.** Run with `QEMUEXTRA="-m 64M"` and then
   `QEMUEXTRA="-m 512M"` (a later `-m` replaces the one in `QEMUFLAGS`).
   Which lines of the map change? With 512 MiB you should see:

   ```
   memory:  639 KiB low, 523136 KiB high
     00100000 - 1ffdffff  available
     1ffe0000 - 1fffffff  reserved
   ```

   Check that `523136` KiB is exactly the size of the range
   `0x100000`–`0x1ffdffff`.

2. **Move `kernel_end`.** Add a global array `char big[65536];` in
   `kmain.c`. Make it a normal global, not `static`: GCC removes a `static`
   variable that nothing reads. Before running, predict the new
   `kernel_end` (it should grow by `0x10000`, plus a little alignment).
   Then check. Which section did `big` go into? Did `build/kernel.elf`
   grow by 64 KiB? (`ls -l build/kernel.elf` before and after.)

   <details>
   <summary>What you should see</summary>

   ```
   kernel:  0x00100000 - 0x00116020 (88 KiB)
   ```

   `big` is in `.bss` (`i686-elf-nm` shows `00102020 B big`), so the kernel
   grew by 64 KiB in memory, but the file grew by only about 200 bytes,
   mostly debug information.

   </details>

3. **The wrong declaration.** Temporarily declare
   `extern uint32_t kernel_end;` and print `kernel_end` with `%lx`. What do
   you get, and why? Then print `&kernel_end`. (You'll get `0`: the 4
   bytes at `kernel_end` happen to be zero. `&kernel_end` is the real
   address.)

4. **Walk the map wrongly.** Change the loop to step by
   `sizeof(struct multiboot_mmap_entry)` (24) instead of `size + 4`. Does
   the output change with QEMU? Why is it still wrong?

## Pitfalls

- **Reading a field without checking its flag.** It works with QEMU, which
  fills in everything, and breaks with another loader.
- **Stepping through the memory map with a fixed size**, or with a pointer
  to the entry structure, instead of `size + 4` bytes.
- **Printing a `uint64_t` with `%lx`.** All later arguments are shifted.
  Cast to `uint32_t`, or print the two halves.
- **`extern uint32_t kernel_end;`** reads the memory at that address
  instead of giving you the address. Declare linker symbols as `char[]`.
- **Overwriting the loader's data.** The command line and the loader's name
  are right after the kernel, at `0x107000`. If you need them later, copy
  them before lesson 6 starts reusing memory.

## References

- Multiboot specification, section 3.3 "Boot information format":
  <https://www.gnu.org/software/grub/manual/multiboot/multiboot.html>
- OSDev wiki: "Detecting Memory (x86)" (why it's hard, and how loaders do
  it) and "Memory Map (x86)" (what's below 1 MiB).
- GNU linker manual, "Assigning Values to Symbols" and "Source Code
  Reference" (why linker symbols have no storage):
  <https://sourceware.org/binutils/docs/ld/Source-Code-Reference.html>
