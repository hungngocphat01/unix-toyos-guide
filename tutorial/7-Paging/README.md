# 7 — Paging

**Goal:** turn paging on with page tables **you** built, and understand
every bit of what the CPU does with them. The kernel still runs at the same
addresses (identity map); lesson 8 moves it.

**Milestone:**

```
vmm: paging enabled, page directory at 0010c000
translate 00104004: PDX=0 PTX=260 offset=004
  CR3=0010c000  PDE[0]=0010d027  -> page table at 0010d000
  PTE[260]=00104003  -> frame 00104000 -> physical 00104004
wrote through alias 0x40000004; secret at 0x00104004 is now cafebabe
translate 40000004: PDX=256 PTX=0 offset=004
  CR3=0010c000  PDE[256]=0014d027  -> page table at 0014d000
  PTE[0]=00104063  -> frame 00104000 -> physical 00104004
reading the alias after unmapping it...
trap 14 (page fault), err=0
  address=40000004 (page not present, read, kernel mode)
  ...
PANIC: unhandled trap 14
```

**Reference code:** [`code/`](code/). New: [`arch/i386/cpu.h`](code/kernel/arch/i386/cpu.h),
[`mm/vmm.h`](code/kernel/mm/vmm.h), [`mm/vmm.c`](code/kernel/mm/vmm.c).
Changed: page-fault details in [`trap.c`](code/kernel/arch/i386/trap.c),
demo in [`kmain.c`](code/kernel/kmain.c).

---

## 1. Building the intuition

### Paging is a dictionary

Forget the trees for a minute. Paging is a **dictionary from virtual page
number to physical frame number** that the CPU consults on *every* memory
access:

```
virtual page number (20 bits)  ──►  physical frame number (20 bits) + permission bits
virtual offset (12 bits)       ──►  copied unchanged
```

A virtual address `0x40000004` is "page `0x40000`, byte `0x004`". Look up
page `0x40000` in the dictionary → frame `0x00104` → physical address
`0x00104004`. That's all translation *is*.

Everything else is a consequence:

- **Two keys can map to the same value.** That's the alias demo: `0x00104004`
  and `0x40000004` are the same byte. It's also how every process will see
  the same kernel, and how shared memory works.
- **A key can be missing.** Accessing it is a **page fault** (vector 14),
  and the CPU tells you the address in CR2. The kernel can then decide:
  kill the program (lesson 12), or fill the page in and retry (demand
  paging: we never do, but lesson 26 lets the program itself catch the fault).
- **Each process gets its own dictionary.** Switching processes = pointing
  CR3 at a different one (lesson 13). Same virtual address `0x00400000`,
  different frames.
- **Entries have permission bits.** Present, writable, user-accessible.
  The kernel's pages lack the "user" bit, so ring 3 can't touch them even
  though they're in the same dictionary.

### Why a two-level tree instead of a flat array?

A flat dictionary for 2²⁰ pages × 4 bytes = **4 MiB per process**, even for
a program that uses 3 pages. So the dictionary is split into chunks of 1024
entries (= one 4 KiB page each), and chunks that would be entirely empty
simply don't exist:

```
                       ┌──── page directory (1 frame, 1024 PDEs) ────┐
 CR3 (physical) ─────► │ [0]  → page table A  (covers 0x00000000-0x003FFFFF)
                       │ [1]  not present     (covers 0x00400000-0x007FFFFF)
                       │ ...
                       │ [256]→ page table B  (covers 0x40000000-0x403FFFFF)
                       │ ...
                       └─────────────────────────────────────────────┘
                                │
          ┌─────────────────────┘
          ▼
   ┌── page table A (1 frame, 1024 PTEs) ──┐
   │ [0]   → frame 0x00000000               │   each PTE covers 4 KiB
   │ ...                                    │   each page table covers 4 MiB
   │ [260] → frame 0x00104000               │   the directory covers 4 GiB
   └────────────────────────────────────────┘
```

So a 32-bit virtual address splits **10 / 10 / 12**:

```
 0x00104004 = 0000000000 0100000100 000000000100
              └─ PDX ──┘ └─ PTX ──┘ └─ offset ─┘
                  0         260         0x004
```

### A translation done by hand (numbers from the milestone)

Translate `0x00104004` (the address of `secret`):

1. `PDX = 0x00104004 >> 22 = 0`, `PTX = (0x00104004 >> 12) & 0x3FF = 260`, `offset = 0x004`.
2. CR3 = `0x0010c000`: the page directory is the frame at physical `0x10c000`.
3. Read PDE[0] = `0x0010d027`. Top 20 bits = page table at `0x0010d000`.
   Low 12 bits `0x027` = `P | W | U | A` (see the bit table below).
4. Read PTE[260] of that table = `0x00104003`. Frame `0x00104000`, flags
   `P | W`. No `U`: user mode can't touch this.
5. Physical = frame + offset = `0x00104004`. (Identity map: same as the virtual address.)

And `0x40000004`: PDX = 256, PTX = 0. PDE[256] points at a page table that
`vmm_map` created for the demo, and its PTE[0] points at **the same frame**
`0x00104000`. `0x063` = `P | W | A | D`: the CPU set **D**irty because
we wrote through it.

### The entry format (PDE and PTE are the same shape)

```
 31                                   12 11  9 8 7 6 5 4 3 2 1 0
┌───────────────────────────────────────┬─────┬─┬─┬─┬─┬─┬─┬─┬─┬─┐
│ physical frame address (top 20 bits)  │avail│G│S│D│A│C│W│U│W│P│
└───────────────────────────────────────┴─────┴─┴─┴─┴─┴─┴─┴─┴─┴─┘
```

| Bit | Name | Meaning |
|---|---|---|
| 0 | P | present. If 0, everything else is ignored (free for the OS to use) |
| 1 | W | writable. If 0, writes fault (from ring 3, and from ring 0 when CR0.WP=1) |
| 2 | U | user. If 0, ring 3 can't access the page |
| 5 | A | accessed. **The CPU** sets it on any access |
| 6 | D | dirty (PTE only). **The CPU** sets it on a write |
| 7 | PS | (PDE only) this PDE maps a 4 MiB page directly, no page table (lesson 8) |
| 9–11 | avail | yours (e.g. "copy-on-write") |

Because frames are 4 KiB-aligned, the low 12 bits of a frame address are
always 0, and Intel reused them for flags. Hence `entry = pa | flags` and
`pa = entry & ~0xFFF`.

Permission checks apply at **both** levels and the strictest wins. So our
PDEs always say `P|W|U` and the PTE decides.

### The one idea that makes this easy: tables are just frames

Page directories and page tables are **ordinary frames from `pmm_alloc()`**.
The trap that confuses everybody:

- the **CPU** follows **physical** addresses: CR3, the frame address in a PDE,
  the frame address in a PTE are all physical;
- **your code** can only use **virtual** addresses (once paging is on, every
  pointer is translated).

So to edit a page table whose physical address you know, you need a virtual
address that maps to it. Our answer: **all physical memory is mapped at
`P2V(pa)`**, so `P2V(PTE_ADDR(*pde))` is always a valid pointer to the page
table. (Today `P2V` is the identity; it still works in lesson 8.) That's
the whole of `walk()`:

```c
static pte_t *walk(pde_t *pd, uint32_t va, int create)
{
    pde_t *pde = &pd[PDX(va)];

    if (!(*pde & PTE_P)) {                      /* no page table yet */
        if (!create)
            return NULL;
        uint32_t pt_pa = pmm_alloc();           /* zeroed = 1024 "not present" */
        if (!pt_pa)
            return NULL;
        *pde = pt_pa | PTE_P | PTE_W | PTE_U;   /* the CPU wants physical */
    }
    pte_t *pt = P2V(PTE_ADDR(*pde));            /* we want virtual */
    return &pt[PTX(va)];
}
```

Every other function is three lines on top of `walk()`.

(Other kernels use "recursive mapping", where the directory maps itself,
to reach page tables. It works, but it's harder to reason about; the direct
map is what Linux and xv6 use on 32-bit.)

### The TLB: the cache you must invalidate

The CPU caches translations in the **TLB**. When you change a PTE that
might be cached (unmap or remap), you must tell it: `invlpg va` for one
page, or reload CR3 to flush everything (except global pages). Forgetting
gives you the hardest bug of all: **the old mapping keeps working for a
while**. Our `vmm_map`/`vmm_unmap` always `invlpg`.

### Turning it on

```c
write_cr3(V2P(kernel_pd));                     /* physical address! */
write_cr0(read_cr0() | CR0_PG | CR0_WP);
```

The instruction after the `mov cr0` is fetched **through the new page
tables**. So the code doing this must be mapped at the same address it's
running at. Our identity map guarantees it. In lesson 8 that becomes the
central problem.

`CR0.WP` makes ring 0 respect read-only pages too. You'll want it for
copy-on-write later; turning it on now costs nothing.

### Page faults

Vector 14, with an error code and CR2 = the faulting address:

| err bit | 0 | 1 |
|---|---|---|
| 0 (P) | page not present | protection violation (page present but forbidden) |
| 1 (W) | read | write |
| 2 (U) | in kernel mode | in user mode |

`trap.c` prints these decoded. In lesson 12, a fault with U=1 kills the
process instead of panicking.

---

## 2. Modules

| File | Responsibility | Exposes |
|---|---|---|
| [`arch/i386/cpu.h`](code/kernel/arch/i386/cpu.h) | control registers and special instructions | `read/write_cr0`, `read_cr2`, `read/write_cr3`, `invlpg`, `cli sti hlt`, `CR0_*`, `EFLAGS_IF` |
| [`mm/vmm.h`](code/kernel/mm/vmm.h) | page-table vocabulary | `pde_t pte_t`, `PTE_P/W/U/PS`, `PDX PTX PTE_ADDR` |
| [`mm/vmm.c`](code/kernel/mm/vmm.c) | build/modify page tables | `kernel_pd`, `vmm_init vmm_map vmm_unmap vmm_translate vmm_explain` |
| [`arch/i386/trap.c`](code/kernel/arch/i386/trap.c) | decode page faults | |

API in words:

- `vmm_map(pd, va, pa, flags)`: make `va` → `pa` in directory `pd`. Creates
  the page table if needed. Panics if `va` is already mapped (a remap is
  almost always a bug).
- `vmm_unmap(pd, va)`: remove it and flush the TLB entry. Doesn't free
  the frame: the caller owns the frame.
- `vmm_translate(pd, va, &pa)`: software walk; you'll use it to check
  user pointers in syscalls (lesson 12).
- `vmm_explain(pd, va)`: a debugging aid. Keep it; call it whenever a page
  fault surprises you.

`vmm_init` maps every frame below `PHYSTOP` at `P2V(pa)`:

```c
for (uint32_t pa = 0; pa < PHYSTOP; pa += PAGE_SIZE)
    vmm_map(kernel_pd, (uint32_t)P2V(pa), pa, PTE_W);
```

With `KERNBASE = 0` that's an identity map. In lesson 8 the **same loop**
produces the higher-half map: nothing in `vmm.c` changes.

## 3. Test

- The milestone demo (it's in [`kmain.c`](code/kernel/kmain.c)).
- QEMU monitor (`make run-nox`, then `Ctrl-A C`): `info mem` lists your
  mappings; `info tlb` too; `xp /4x 0x10c000` dumps the page directory's
  first entries. `Ctrl-A C` again to return.
- Remove the `invlpg` in `vmm_unmap`: the last read in the demo may
  *succeed*. That's the TLB serving a stale translation.
- Map the alias **without** `PTE_W` and write to it: protection violation,
  write, kernel mode (thanks to CR0.WP).

## Pitfalls

- **Writing a virtual address into CR3 or a PDE.** Works today (identity!),
  explodes in lesson 8. Always `V2P()` when giving an address to the CPU.
- **Not zeroing a new page table**: garbage "present" PTEs map random memory.
  (Our `pmm_alloc` zeroes.)
- **Mapping only the kernel image, not the page tables or the stack**:
  first instruction after enabling paging faults → triple fault. The QEMU
  log shows `v=0e` with `CR2` = the unmapped address.
- **Forgetting `invlpg`**.
- **Freeing a frame that's still mapped**: the PMM gives it to someone
  else, and two parties share it unknowingly.

## References

- Intel SDM Vol. 3A ch. 4 "Paging": §4.3 (32-bit paging), Figure 4-2
  (the translation diagram), Table 4-5/4-6 (PDE/PTE formats), §4.7 (page-fault
  error code), §4.10 (TLB).
- OSDev: "Paging", "TLB", "Page Frame Allocation"
- xv6: `vm.c` (`walkpgdir`, `mappages`), `mmu.h`. Ours is the same design.
