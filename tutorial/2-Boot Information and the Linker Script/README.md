# 2 — Boot Information and the Linker Script

**Goal:** know where RAM is, where your kernel is, and what the loader told
you. The physical memory manager (lesson 6) is built on exactly this.

**Milestone** (`make run QEMUEXTRA="-append hello"`):

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
```

**Reference code:** [`code/`](code/). Changes since lesson 1:
[`multiboot.h`](code/kernel/arch/i386/multiboot.h) (new),
[`linker.ld`](code/kernel/arch/i386/linker.ld) (`kernel_start`),
[`kmain.c`](code/kernel/kmain.c), and `$(QEMUEXTRA)` in the
[`Makefile`](code/Makefile) so you can pass extra QEMU options.

---

## Concepts

### What the loader leaves you

`EBX` points to a `struct multiboot_info`. Its first field, `flags`, says
which of the other fields are valid. **Always check the flag before reading
a field.** Field layout (spec section 3.3):

| Offset | Field | Valid if flag bit |
|---|---|---|
| 0 | `flags` | always |
| 4, 8 | `mem_lower`, `mem_upper` (KiB) | 0 |
| 16 | `cmdline` (physical address of a C string) | 2 |
| 20, 24 | `mods_count`, `mods_addr` | 3 |
| 44, 48 | `mmap_length`, `mmap_addr` | 6 |
| 64 | `boot_loader_name` | 9 |

**Every address in it is physical.** Right now that doesn't matter (paging
is off, so physical = virtual). In lesson 8, when the kernel moves to
`0xC0000000`, every one of these pointers will need converting. Remember it.

### The memory map

`mem_upper` alone isn't enough: RAM has holes (the ACPI tables, the
`0xFFFC0000` BIOS ROM, the gap at 640 KiB...). The memory map is a list of
`(base, length, type)` entries; only `type == 1` is usable RAM.

The entries have a quirk: each starts with a `size` field that **doesn't
count itself**, and entries may be bigger than the struct you know. So you
walk the list with the size, never with `e++`:

```c
uint32_t addr = mbi->mmap_addr;
while (addr < mbi->mmap_addr + mbi->mmap_length) {
    struct multiboot_mmap_entry *e = (struct multiboot_mmap_entry *)addr;
    /* use e->addr, e->len, e->type */
    addr += e->size + sizeof(e->size);
}
```

The struct must be `__attribute__((packed))`: `addr` is a `uint64_t` at
offset 4, and GCC would otherwise insert padding to align it.

### Reading the map above

```
00000000 - 0009fbff  available   "low memory": real-mode IVT, BIOS data. Usable,
                                 but the Multiboot info itself lives here (0x9500)!
0009fc00 - 000fffff  reserved    EBDA, VGA memory (0xA0000-0xBFFFF), BIOS ROMs
00100000 - 07fdffff  available   the 127 MiB you'll actually manage. Kernel at its start.
07fe0000 - 07ffffff  reserved    ACPI tables
fffc0000 - ffffffff  reserved    the BIOS ROM, mapped at the top of 4 GiB
```

Our policy (lesson 6): **never use anything below 1 MiB**. It's under 640
KiB of awkward memory that holds the boot info, and not worth the bugs.

### Linker-script symbols

A symbol defined in a linker script has an address but **no storage**.
`kernel_end = .;` means "the address where the linker is at this point".
You declare it in C as an array and only use its address:

```c
extern char kernel_start[], kernel_end[];
kprintf("%p - %p\n", kernel_start, kernel_end);
```

Declaring it `extern uint32_t kernel_end;` and reading `kernel_end` would
read whatever bytes happen to be at that address. Classic bug.

Look at where things landed:

```sh
i686-elf-nm -n build/kernel.elf | head -30
i686-elf-readelf -l build/kernel.elf      # LOAD segments: what the loader copies
```

`.bss` is in the image's address range but takes **no space in the file**:
the loader zeroes it (a `LOAD` segment with `MemSiz > FileSiz`). That's why
the kernel is 24 KiB in memory (16 KiB is the boot stack) but the
file is small. Multiboot loaders zero `.bss` for you; a homemade bootloader
would have to.

### Passing arguments to the kernel

`QEMUEXTRA="-append 'root=hda debug'"` sets the command line. QEMU puts the
kernel path first. Later you can use this to pass options like a
`init=/bin/sh` override.

---

## Modules

| File | Change |
|---|---|
| [`arch/i386/multiboot.h`](code/kernel/arch/i386/multiboot.h) | **new**: the structs and flag bits. Only the fields we use are named. |
| [`arch/i386/linker.ld`](code/kernel/arch/i386/linker.ld) | add `kernel_start = .;` right after `. = 1M;` |
| [`kmain.c`](code/kernel/kmain.c) | `print_boot_info()`; cast `mbi_phys` to a pointer |

## Test

- Compare your memory map with `-m 64M` and `-m 512M`
  (`make run QEMUEXTRA="-m 512M"`; a later `-m` overrides the earlier one).
- Add a 64 KiB `static char big[65536];` somewhere and see `kernel_end` move.

## Pitfalls

- Reading `mmap` fields without checking `flags & (1 << 6)`.
- Walking the map with `e++` instead of `size + 4`.
- Forgetting `packed` (all the addresses look shifted by 4 bytes).
- Printing a `uint64_t` with `%x` passes 8 bytes for a 4-byte specifier and
  shifts every later argument. Cast to `uint32_t` (we're below 4 GiB).

## References

- Multiboot spec, section 3.3 "Boot information format"
- OSDev: "Detecting Memory (x86)", "Memory Map (x86)"
- `ld` manual: "Assigning Values to Symbols" (`info ld`, or sourceware.org)
