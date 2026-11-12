# 9 — Kernel Heap

**Goal:** `kmalloc(size)` / `kfree(p)` for the small objects the rest of
the kernel needs: process structs, open-file structs, in-memory inodes, pipe
buffers, path strings.

**Milestone:**

```
kheap test: kmalloc(10)=0xc014ffe8 kmalloc(100)=0xc0150f88 kmalloc(1000)=0xc0151c08
kheap test: freed 0xc0150f88, kmalloc(120)=0xc0150f88, a="hello"
kheap test: ok
```

**Reference code:** [`code/`](code/). New: [`mm/kheap.h`](code/kernel/mm/kheap.h),
[`mm/kheap.c`](code/kernel/mm/kheap.c). `kmain.c` runs `kheap_test()`.

---

## Concepts

### What the kernel actually needs

A general-purpose `malloc` (any size, low fragmentation, fast) is a research
topic. A small kernel needs much less:

- objects are **small** (≤ a few hundred bytes) and come in a **few fixed
  sizes** (one per struct type);
- whole pages (page tables, kernel stacks, process memory) come straight
  from `pmm_alloc()`;
- no need to give memory back to the PMM: the number of processes, files
  and inodes goes up and down, but the peak is small.

### Size classes

So: round every request up to a power of two (32, 64, ..., 4096 bytes), and
keep **one free list per size**. When a list is empty, take a page from the
PMM and cut it into blocks of that size:

```
free_lists[5]  (32 B)  → blk → blk → blk → ... (a page cut into 128 blocks)
free_lists[6]  (64 B)  → blk → blk → ...
free_lists[7]  (128 B) → blk → ...
...
free_lists[12] (4 KiB) → blk
```

The free list is stored **inside the free blocks themselves** (the first
4 bytes of each free block point to the next one), so it needs no memory of
its own. That's the same trick xv6 uses for frames.

### How does `kfree` know the size?

Each block starts with an 8-byte header written by `kmalloc`:

```
 block of 128 bytes:
 ┌───────────┬───────────┬──────────────────────────────────────┐
 │ magic     │ shift = 7 │ 120 bytes for the caller              │
 └───────────┴───────────┴──────────────────────────────────────┘
                         ▲ kmalloc returns this pointer
```

`kfree(p)` steps back 8 bytes, reads `shift`, and pushes the block onto
`free_lists[shift]`. The **magic number** catches the two most common heap
bugs on the spot: freeing a pointer that didn't come from `kmalloc`, and
freeing the same block twice (`kfree` clears the magic).

So `kmalloc(120)` lands in the 128-byte class, which is why the test got
`b`'s block back. `kmalloc(4090)` would need 4098 bytes: too big. Use
`pmm_alloc()` for page-sized things.

The core of [`kheap.c`](code/kernel/mm/kheap.c):

```c
void *kmalloc(size_t size)
{
    size_t total = size + sizeof(struct header);
    int shift = MIN_SHIFT;
    while ((1u << shift) < total)
        shift++;
    if (shift > MAX_SHIFT)
        panic("kmalloc: %lu bytes is too big", size);
    if (!free_lists[shift] && refill(shift) < 0)
        return NULL;

    struct free_block *b = free_lists[shift];       /* pop */
    free_lists[shift] = b->next;

    struct header *h = (struct header *)b;
    h->magic = MAGIC;
    h->shift = shift;
    memset(h + 1, 0, (1u << shift) - sizeof(struct header));
    return h + 1;
}
```

Like `pmm_alloc`, `kmalloc` **returns zeroed memory**. Freshly allocated
structs with all-zero fields (NULL pointers, count 0) prevent a lot of bugs
for very little time.

### Where the memory comes from

`refill()` calls `pmm_alloc()` and uses `P2V(pa)`: heap blocks are inside the
direct map, at `0xC0xxxxxx`. No new page tables and no "heap region" to
manage: the direct map from lesson 8 already covers every frame. That's why
the addresses in the milestone look like `0xc014ffe8`.

---

## Modules

| File | Responsibility | Exposes |
|---|---|---|
| [`mm/kheap.c`](code/kernel/mm/kheap.c) | size-class allocator on top of the PMM | `kmalloc(size)`, `kfree(p)`, `KMALLOC_MAX` |

No `kheap_init()` is needed: the free lists start empty and fill on demand.

## Test

- The milestone.
- `kfree` the same pointer twice → panic with a clear message.
- Allocate 1000 × 100 bytes; check `pmm_free_count()` dropped by
  1000 / 32 blocks-per-page ≈ 32 pages.
- Write past the end of a block (`memset(p, 'x', 200)` on a 100-byte block)
  and `kfree` the *next* block: magic check fails. That's what heap
  corruption looks like.

## Pitfalls

- **`kmalloc` in an interrupt handler while the main code is also in
  `kmalloc`**: the free list gets corrupted. Not possible yet (no handler
  allocates), but remember it for lesson 24: disable interrupts around
  allocator updates, or never allocate in interrupt handlers.
- **Using memory after `kfree`**: the first 4 bytes now hold the free-list
  pointer, so freed objects look "mostly fine" except their first field.
- **Assuming `kmalloc` can return big buffers**: use pages.

## References

- OSDev: "Memory Allocation", "Writing a memory manager"
- The size-class idea is a simplified **slab allocator**: Bonwick, "The
  Slab Allocator" (USENIX 1994).
- V7 had no kernel heap: every table (`proc[]`, `inode[]`, `file[]`) was a
  fixed-size array (`NPROC`, `NINODE`, `NFILE` in `param.h`). That's a fine
  choice too, and we'll use fixed arrays where V7 did.
