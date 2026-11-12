# 15 — Buffer Cache

**Goal:** a cache of disk blocks with V7's interface (`bread`, `bwrite`,
`brelse`), and a **device switch** so the cache doesn't care which driver is
behind a device number.

**Milestone:**

```
ata: "QEMU HARDDISK", 16384 sectors (8192 KiB)
bio: block 0 starts with "toyos test disk, sector 0"
bio: second bread(0) returned the same buffer
bio: block 5 read back: "written through the buffer cache"
bio: 1 hits, 43 misses
```

**Reference code:** [`code/`](code/). New: [`dev/dev.h`](code/kernel/dev/dev.h),
[`dev/dev.c`](code/kernel/dev/dev.c), [`fs/bio.h`](code/kernel/fs/bio.h),
[`fs/bio.c`](code/kernel/fs/bio.c). `kmain.c` runs `bio_test()`.

---

## Concepts

### Blocks, not sectors

Filesystems work in **blocks**: here 1024 bytes = 2 sectors (ext2's
smallest block size, and the one we'll format with). The block layer
translates: block `n` = sectors `2n` and `2n+1`.

### Device numbers and the device switch (a V7 idea)

The kernel names a device with a 16-bit **`dev_t`**: **major** (which
driver) and **minor** (which unit of that driver):

```
 dev_t 0x0000 = major 0 (ATA disk), minor 0 (first disk)
 dev_t 0x0001 = major 0 (ATA disk), minor 1 (a second disk)
```

The **block device switch** `bdevsw[]` maps a major number to that driver's
functions. Code above it calls `bdevsw[MAJOR(dev)].read(...)` and never
names a driver. Adding a RAM disk, or a second disk type, means one new
table entry. V7's `conf.c` is exactly this table, and lesson 17 adds the
character device switch, `cdevsw[]`, for the console and `/dev/null`.

```c
struct bdevsw bdevsw[] = {
    [BDEV_ATA] = { ata_block_read, ata_block_write },
};
```

Device numbers also end up **on disk**: a device file such as `/dev/console`
is an inode whose content is just a `dev_t` (lesson 17).

### The cache

Without a cache, reading one byte of a file costs several disk reads (the
inode's block, an indirect block, the data block), and the same few blocks
(superblock, inode tables, `/`, `/bin`) are read over and over.

So the kernel never calls the driver directly. Everything goes through a
pool of `NBUF` buffers:

```
              most recently used ◄────────────────────► least recently used
 lru ⇄ [dev 0, blk 2] ⇄ [dev 0, blk 1] ⇄ [dev 0, blk 37] ⇄ ... ⇄ [dev 0, blk 0] ⇄ lru
         refcnt 1         refcnt 0         refcnt 0                refcnt 0
```

- **`bread(dev, n)`**: look for block `n`; if found it's a **hit** and no disk
  access happens. Otherwise take the **least recently used** buffer with
  `refcnt == 0` (nobody is using it), relabel it, read the block into it.
- **`brelse(b)`**: "I'm done with it". Decrement `refcnt` and move it to the
  front (most recently used). It stays cached.
- **`bwrite(b)`**: write it to the disk now.

```c
struct buf *b = bread(ROOTDEV, 0);
/* ... read or modify b->data ... */
bwrite(b);                      /* only if modified */
brelse(b);                      /* always, exactly once per bread */
```

**Every `bread` needs exactly one `brelse`**, like `malloc`/`free`. If you
forget one, that buffer can never be recycled; after `NBUF` leaks,
`bread` panics with "all buffers are in use". That panic is how you'll find
the leak.

### Why one buffer per block matters

`getblk` guarantees that a block is in **at most one** buffer. If two parts
of the kernel `bread` the same inode block, they get the **same** buffer, so
one's modification is seen by the other. With two copies, the second
`bwrite` would silently overwrite the first one's change.

### Write-through vs delayed write

We write immediately (`bwrite` = disk write). V7 also had `bdwrite`
(mark dirty, write later when the buffer is recycled or at `sync`): much
faster for metadata that changes often, but anything not yet written is lost
if the machine dies. Write-through is the simple and safe start; `sync`
(lesson 19) is then a no-op.

### Locking (not yet)

V7's `struct buf` has a `B_BUSY` flag: a process that wants a buffer
someone else is using **sleeps** until it's released. Our processes don't run
concurrently yet, so `refcnt` is enough. Lesson 24 explains why it still is.

---

## Modules

| File | Responsibility | Exposes |
|---|---|---|
| [`dev/dev.h`](code/kernel/dev/dev.h) | `dev_t`, `MAJOR/MINOR/MKDEV`, `struct bdevsw`, `BLOCK_SIZE`, `ROOTDEV` | |
| [`dev/dev.c`](code/kernel/dev/dev.c) | the `bdevsw[]` table; block→sector adapter for ATA | `bdevsw`, `nbdev` |
| [`fs/bio.c`](code/kernel/fs/bio.c) | the cache | `bio_init bread bwrite brelse bio_stats` |

Layering after this lesson:

```
 (lesson 16+) ext2 ──► bread/bwrite/brelse ──► bdevsw[major] ──► ata_read/ata_write
```

## Test

- The milestone. The 43 misses are: block 0, block 5, 40 eviction blocks,
  block 5 again (it was evicted). The 1 hit is the second `bread(0)`.
- Leave out one `brelse` inside a loop of 40 reads: the panic message.
- `bread` the same block twice without releasing: same pointer, `refcnt 2`.

## Pitfalls

- **Missing `brelse`** (leak) or **double `brelse`** (assert).
- **Holding a `struct buf *` after `brelse`** and using it later: it may have
  been recycled for a different block in between.
- **Comparing only `blockno`** in `getblk`, not `dev`: with two disks, the
  wrong data.
- Putting a large `NBUF` in `.bss`: the kernel image must still fit in the
  16 MiB boot mapping (lesson 8). 32 × 1 KiB is fine.

## References

- V7: `usr/sys/dev/bio.c` (`bread`, `getblk`, `brelse`, `bdwrite`),
  `usr/sys/h/buf.h`, `usr/sys/conf/c.c` (`bdevsw`, `cdevsw`).
- Lions' Commentary, chapter 15–16 ("Block I/O").
- xv6: `bio.c`, `buf.h`.
