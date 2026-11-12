# 6 — Physical Memory Manager

**Goal:** a module that answers one question: "give me a free 4 KiB piece of
RAM" (and "I'm done with this one"). Page tables, the kernel heap, process
memory and disk buffers are all built on it.

**Milestone:**

```
pmm: 32468 frames free (126 MiB)
pmm test: allocated 10c000 10d000 10e000
pmm test: freed 10d000, next alloc gave 10f000
pmm test: ok
idle
```

**Reference code:** [`code/`](code/). New: [`mm/memlayout.h`](code/kernel/mm/memlayout.h),
[`mm/pmm.h`](code/kernel/mm/pmm.h), [`mm/pmm.c`](code/kernel/mm/pmm.c).

---

## Concepts

### Frames

Physical memory is cut into **frames**: 4 KiB, aligned on 4 KiB. Frame *n*
covers physical addresses `n*4096 .. n*4096+4095`. 4 KiB because that's the
size of a page in the x86 page tables (lesson 7): every page of virtual
memory will be backed by exactly one frame.

The PMM tracks which frames are free. It doesn't care what they're used
for, and it hands out **physical addresses** (a `uint32_t`, *not* a
pointer; see `P2V` below).

### Bitmap vs free list

| | Bitmap (our choice) | Free list (xv6's `kalloc`) |
|---|---|---|
| Idea | 1 bit per frame | each free frame stores a pointer to the next free frame |
| Memory | 8 KiB for 256 MiB | none (lives in the free frames) |
| Alloc | scan for a 0 bit | pop the head: O(1) |
| Debugging | "is this frame free?" is one bit test; double-free detection is free | a double free silently corrupts the list |
| Contiguous ranges | easy to add | hard |

The bitmap's double-free detection alone is worth it while you're learning.

### Building the bitmap from the memory map

Start pessimistic and carve out what's safe:

```
1. every frame = used
2. for each memory-map entry with type 1 (RAM):
       free the frames completely inside it, but only in [1 MiB, PHYSTOP)
3. mark used again: the kernel image  [kernel_start, kernel_end)
                    the boot modules   (lesson 12: the loader puts them after the kernel)
```

Rounding goes **inward** when freeing (a partial frame isn't usable) and
**outward** when reserving (a partially-used frame is used):

```c
static void free_range(uint64_t start, uint64_t end)
{
    start = PAGE_ROUND_UP(start);
    end = PAGE_ROUND_DOWN(end);
    ...
}
static void reserve_range(uint32_t start, uint32_t end)
{
    start = PAGE_ROUND_DOWN(start);
    end = PAGE_ROUND_UP(end);
    ...
}
```

**Why ignore everything below 1 MiB?** It's only ~640 KiB, it contains
the BIOS data area and the Multiboot info (at `0x9500` with QEMU), and
some old hardware can only DMA there. Not worth it.

**Why stop at `PHYSTOP` (256 MiB)?** From lesson 8 on, the kernel can only
touch physical memory that is mapped at `KERNBASE + phys`, and we'll map
256 MiB. A frame the kernel can't reach is useless to it. (V7 ran
in a few hundred KiB, so this won't be what limits you.)

### `P2V` / `V2P`: a habit to start now

The PMM returns **physical** addresses. To *write* into a frame (to
zero it, say), the kernel needs a **virtual** address that points at it.
Right now paging is off, so they're equal. From lesson 8 they won't be:
physical `pa` will be visible at virtual `pa + 0xC0000000`.

So from today, **every** conversion goes through two macros in
[`mm/memlayout.h`](code/kernel/mm/memlayout.h):

```c
#define KERNBASE    0x00000000                                  /* lesson 8: 0xC0000000 */
#define P2V(pa) ((void *)((uintptr_t)(pa) + KERNBASE))          /* phys -> pointer */
#define V2P(va) ((uintptr_t)(va) - KERNBASE)                    /* pointer -> phys */
```

Today they do nothing. In lesson 8 you change `KERNBASE`, and code that
used them consistently keeps working. Typical uses:

```c
memset(P2V(pa), 0, PAGE_SIZE);                     /* zero a frame        */
struct multiboot_info *mbi = P2V(mbi_phys);        /* loader's pointers   */
reserve_range(V2P(kernel_start), V2P(kernel_end)); /* where is the kernel */
```

Rule of thumb: **a `uint32_t` holding a physical address never gets
dereferenced**; a pointer is always virtual.

### API

```c
void     pmm_init(struct multiboot_info *mbi);
uint32_t pmm_alloc(void);       /* zeroed frame, or 0 when out of memory */
void     pmm_free(uint32_t pa);
uint32_t pmm_free_count(void);
```

Returning **zeroed** frames costs a little time and removes a whole class
of bugs: page tables full of garbage "present" entries, processes reading
the previous owner's data. (0 is never a valid frame because the first
MiB is never handed out, so it can mean "no memory".)

`next_hint` makes allocation resume where the last one stopped instead of
rescanning from frame 0. That's why the test gets `10f000` instead of the
just-freed `10d000`: not a bug, both are correct.

---

## Modules

| File | Responsibility | Exposes |
|---|---|---|
| [`mm/memlayout.h`](code/kernel/mm/memlayout.h) | the memory constants | `PAGE_SIZE PHYSTOP KERNBASE P2V V2P PAGE_ROUND_UP/DOWN` |
| [`mm/pmm.c`](code/kernel/mm/pmm.c) | frame bitmap | `pmm_init pmm_alloc pmm_free pmm_free_count` |
| [`kmain.c`](code/kernel/kmain.c) | `pmm_init(P2V(mbi_phys))` + `pmm_test()` | |

The PMM depends on `multiboot.h` only in `pmm_init`. Nothing else in the
kernel should parse the memory map.

## Test

- The milestone test, plus: free a frame twice → panic. Free `0x1000` → assert.
- Allocate frames in a loop until `pmm_alloc()` returns 0; the count should
  equal the "frames free" line. With `-m 64M` it should be ~16000.
- Write a frame's own address into it after allocating, and check it
  hasn't been overwritten by the time you free it (catches overlapping
  ranges with the kernel).

## Pitfalls

- **Handing out the kernel's own frames** (forgot to reserve
  `kernel_start..kernel_end`): the first allocation zeroes your code. The
  symptom is a crash in a random place right after `pmm_alloc()`.
- **Forgetting the modules** (lesson 12): your `init` program gets zeroed
  before you load it.
- **Using `kernel_end` as a variable** instead of an address (lesson 2).
- **Rounding the wrong way**, giving away a frame that's half reserved.
- **64-bit entries**: the map's `addr`/`len` are `uint64_t`. Compare in
  64 bits before clamping, or a region at 4 GiB+ wraps around to 0.

## References

- OSDev: "Page Frame Allocation", "Detecting Memory (x86)"
- xv6: `kalloc.c` (free-list version)
- V7 had no page frames at all: it swapped whole processes (`usr/sys/sys/malloc.c`
  manages *core map* segments). Paging came with 3BSD on the VAX.
