# 8 — Higher-Half Kernel

**Goal:** move the kernel to virtual `0xC0000000` and up, leaving
`0x00000000–0xBFFFFFFF` free for user programs. After this lesson, every
process will have the kernel mapped at the same place, in the top quarter
of its address space.

**Milestone:**

```
pmm: 32466 frames free (126 MiB)
vmm: kernel page directory at 0010e000
higher half:
  kmain        virtual c0102a10 -> physical 00102a10
  stack        virtual c010d904 -> physical 0010d904
  VGA          virtual c00b8000 -> physical 000b8000
  old 1 MiB    virtual 00100000 -> not mapped
```

**Reference code:** [`code/`](code/). Changed:
[`boot.asm`](code/kernel/arch/i386/boot.asm) (early paging),
[`linker.ld`](code/kernel/arch/i386/linker.ld) (VMA ≠ LMA),
[`memlayout.h`](code/kernel/mm/memlayout.h) (`KERNBASE = 0xC0000000`),
[`vga.c`](code/kernel/dev/vga.c) (`P2V(0xB8000)`), and comments in
[`vmm.c`](code/kernel/mm/vmm.c). That's all: thanks to `P2V`/`V2P`,
`pmm.c` and `vmm.c` didn't need code changes.

---

## Concepts

### Why the higher half?

User programs are linked at low addresses (`0x00400000`). The kernel must be
mapped in **every** address space, so that an interrupt arriving while a
user program runs can execute kernel code without switching CR3. So the
two must not overlap, and the classic split is:

```
0x00000000 ─ 0xBFFFFFFF   3 GiB   user, different in every process
0xC0000000 ─ 0xFFFFFFFF   1 GiB   kernel, identical in every process
```

Each process's page directory then has **its own entries 0–767** and
**shared entries 768–1023** (lesson 10 copies them from `kernel_pd`).

### Two addresses for every byte of the kernel

The kernel is still **loaded** at physical 1 MiB (there may be no RAM at
3 GiB!). But it's **linked** to run at `0xC0100000`: every absolute address
in the code (`call kmain`, `&secret`, string literals) is `0xC01xxxxx`.

The linker script expresses this with two addresses per section:

```
.text ALIGN(4K) : AT(ADDR(.text) - KERNBASE)
```

- **VMA** (virtual memory address) = `ADDR(.text)` ≈ `0xC0101000`: what the
  code uses.
- **LMA** (load memory address) = `AT(...)` ≈ `0x00101000`: where the loader
  copies it.

`readelf -l` shows both:

```
  Type  Offset   VirtAddr   PhysAddr   FileSiz MemSiz  Flg
  LOAD  0x001000 0x00100000 0x00100000 0x00030 0x00030 R      ← .boot (multiboot header + _start)
  LOAD  0x002000 0xc0101000 0x00101000 0x03bc0 0x03bc0 R E    ← .text
  LOAD  0x006000 0xc0105000 0x00105000 0x02000 0x08920 RW     ← .data + .bss
```

### The chicken-and-egg problem, and the trampoline

The loader jumps to us with paging **off**, but our code is linked for
addresses that only exist with paging **on**. Any absolute address used
before paging is on points at nonexistent RAM. So:

1. **A tiny bit of code runs at its physical address.** `_start` lives in
   section `.boot`, linked at 1 MiB (not `+ KERNBASE`). It uses only
   relative jumps and addresses it computes as `symbol - KERNBASE`.
2. **It turns paging on with a temporary page directory** that maps the
   first 16 MiB **twice**:

   ```
   virtual 0x00000000-0x00FFFFFF → physical 0-16 MiB    identity: so the instruction
                                                        after "mov cr0" still exists
   virtual 0xC0000000-0xC0FFFFFF → physical 0-16 MiB    where the kernel is linked
   ```

3. **It jumps to an absolute high address**: from then on EIP is `0xC01xxxxx`.
4. **C code builds the real page tables** (`vmm_init`, unchanged from
   lesson 7) and loads CR3. The identity map disappears.

```
              paging off       │ paging on, boot_pd              │ paging on, kernel_pd
 EIP:  0x001000xx (.boot) ─────┼─► still 0x001000xx ──jmp ecx──► │ 0xC01xxxxx ...
                               │   (identity part)    0xC01xxxxx │
       mov cr3, boot_pd-KERNBASE                                 vmm_init(): mov cr3
       mov cr0 |= PG                                             (identity gone)
```

### The boot page directory, with 4 MiB pages

Writing page tables in assembly is tedious, so the boot directory uses
**4 MiB pages**: a PDE with the PS bit (`0x80`) maps 4 MiB directly, no page
table. Enable it with `CR4.PSE`. Four PDEs = 16 MiB, and it's plain data:

```nasm
section .data
align 4096
boot_pd:
    dd 0x00000000 | PDE_4M, 0x00400000 | PDE_4M, 0x00800000 | PDE_4M, 0x00C00000 | PDE_4M
    times (KERNBASE >> 22) - 4 dd 0                          ; PDE 4..767: empty
    dd 0x00000000 | PDE_4M, 0x00400000 | PDE_4M, 0x00800000 | PDE_4M, 0x00C00000 | PDE_4M
    times 1024 - (KERNBASE >> 22) - 4 dd 0                   ; PDE 772..1023: empty
```

`KERNBASE >> 22 = 768`: the directory index of `0xC0000000`.

The trampoline itself ([`boot.asm`](code/kernel/arch/i386/boot.asm)):

```nasm
section .boot
_start:
    mov ecx, cr4
    or ecx, CR4_PSE
    mov cr4, ecx
    mov ecx, boot_pd - KERNBASE     ; physical address of boot_pd
    mov cr3, ecx
    mov ecx, cr0
    or ecx, CR0_PG
    mov cr0, ecx
    mov ecx, higher_half            ; absolute: 0xC01xxxxx
    jmp ecx                         ; (a plain "jmp higher_half" would be
                                    ;  PC-relative and stay low!)
section .text
higher_half:
    mov esp, stack_top              ; a high address now
    push ebx
    push eax
    call kmain
```

It uses only ECX because EAX/EBX carry the Multiboot magic and info pointer.

**Why 16 MiB?** Until `vmm_init` finishes, only that much is reachable.
The kernel image, the boot stack and the frames `vmm_init` allocates for
page tables (the PMM hands out the lowest free frames first) must fit.
If your kernel grows large static arrays (lesson 15's buffer cache), check
`kernel_end`.

### What else must change? Anything that used a physical address as a pointer

The loader's information is physical. That's why we've been writing
`P2V()` since lesson 6:

| What | Before | Now |
|---|---|---|
| VGA buffer | `0xB8000` | `P2V(0xB8000)` |
| Multiboot info, memory map, modules | `mbi_phys` | `P2V(mbi_phys)` (already done) |
| Page tables in `walk()` | `P2V(PTE_ADDR(*pde))` | unchanged |
| CR3 | `V2P(kernel_pd)` | unchanged |
| GDT/IDT base | `(uint32_t)gdt` | unchanged: `lgdt` takes a **virtual** (linear) address |

If you missed one, it now page-faults at a low address. That's the point
of dropping the identity map: bugs show up immediately.

---

## Modules

| File | Change |
|---|---|
| [`arch/i386/boot.asm`](code/kernel/arch/i386/boot.asm) | `boot_pd`, `.boot` trampoline, `higher_half` |
| [`arch/i386/linker.ld`](code/kernel/arch/i386/linker.ld) | `.boot` low; everything else `+ KERNBASE` with `AT(...)`; `kernel_start` is now a high address |
| [`mm/memlayout.h`](code/kernel/mm/memlayout.h) | `KERNBASE 0xC0000000` |
| [`dev/vga.c`](code/kernel/dev/vga.c) | `P2V(0xB8000)` |
| [`mm/vmm.c`](code/kernel/mm/vmm.c) | comments only: paging is already on, so `vmm_init` just loads CR3 |

`KERNBASE` is now defined in three places (C header, NASM, linker script)
because they can't include each other. Keep a comment in each pointing at
the others.

## Steps

1. Change the linker script, keeping `KERNBASE` at 0 in C. Check
   `readelf -l`: still boots (VMA = LMA).
2. Add `boot_pd` and the trampoline with KERNBASE = `0xC0000000` in the
   linker script and NASM. Before calling `kmain`, write a character to
   `0xC00B8000` from assembly to prove you're up there.
3. Flip `KERNBASE` in `memlayout.h`. Fix every page fault at a low address.

## Test

- The milestone.
- `make debug` + `make gdb`: `break *0x100010` (physical `_start`) then
  `stepi` through the trampoline; `info registers eip` before and after
  `jmp ecx`. (GDB symbols use VMAs, so `break _start` works too.)
- Dereference `0xB8000` from C: page fault at `000b8000`. Good.

## Pitfalls

- **Triple fault on `mov cr0`**: CR3 got a virtual address (forgot
  `- KERNBASE`), or `boot_pd` isn't 4 KiB-aligned, or `CR4.PSE` isn't set.
  QEMU log: `v=0e` with CR2 near EIP.
- **`jmp higher_half` (relative) instead of via a register**: NASM emits a
  PC-relative jump, which is correct for the *link* addresses... and lands
  at the wrong place when run from the physical copy. Use the register.
- **Multiboot "no header found"**: `.multiboot` must stay in the first
  8 KiB of the *file*.
- **Kernel larger than the boot map**: crash at a high address above
  `0xC1000000` before `vmm_init`.
- **Loader can't load the image**: QEMU uses the LMA (`PhysAddr`). If it's
  `0xC01xxxxx`, you forgot `AT(...)`.

## References

- OSDev: "Higher Half Kernel", "Higher Half x86 Bare Bones"
- `ld` manual: "Output Section LMA"
- xv6: `entry.S` + `entrypgdir` in `main.c` (same trick, 4 MiB pages),
  `kernel.ld`
