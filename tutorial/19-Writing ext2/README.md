# 19 — Writing ext2

**Goal:** create, write, link, unlink and delete files and directories on
the ext2 disk, with V7's system calls: `creat`, `write`, `link`, `unlink`,
`mknod`, `chmod`, `chown`, `sync`, plus `time` (a clock, for timestamps).
Then let the real `e2fsck` judge the result.

**Milestone:**

```
time: 1794407815 seconds since 1970 (day 20768)
boot number 1
creat, write, read:
  ok   creat /tmp/hello
  ...
link, unlink:
  ok   link /tmp/hello /tmp/hi
  ok   2 links
  ...
  ok   an unlinked file stays readable while open
a 300 KiB file (single and double indirect blocks):
  ok   size is 307200
  ok   every block reads back
  ok   unlink it (frees ~303 blocks)
directories:
  ok   mkdir /tmp/d
  ...
0 failure(s)
```

and then, on your Mac:

```
$ /opt/homebrew/opt/e2fsprogs/sbin/e2fsck -fn build/disk.img
Pass 1: Checking inodes, blocks, and sizes
Pass 2: Checking directory structure
Pass 3: Checking directory connectivity
Pass 4: Checking reference counts
Pass 5: Checking group summary information
build/disk.img: 28/2048 files (0.0% non-contiguous), 490/8192 blocks
```

No complaints means your kernel wrote a filesystem that Linux would mount.
Boot again: `boot number 2`, so the changes are really on the disk.

**Reference code:** [`code/`](code/). New: [`dev/rtc.c`](code/kernel/dev/rtc.c)
(CMOS clock), user [`mkdir.c`](code/user/bin/mkdir.c) and
[`fstest.c`](code/user/bin/fstest.c). Changed:
[`fs/super.c`](code/kernel/fs/super.c) (`balloc bfree ialloc ifree`),
[`fs/inode.c`](code/kernel/fs/inode.c) (`iupdate`, allocating `bmap`, `writei`,
`itrunc`, deleting in `iput`), [`fs/dir.c`](code/kernel/fs/dir.c) (`dirlink`,
`dirunlink`, `namei_parent`), [`fs/file.c`](code/kernel/fs/file.c)
(`file_write` → `writei`), [`sys/sys_file.c`](code/kernel/sys/sys_file.c)
(the new system calls), user libc wrappers.

---

## 1. What changes on disk when you write?

Reading needed only *finding* things. Writing changes several structures,
and **they must stay consistent with each other**. Here is everything
`creat("/tmp/a")` + `write(fd, "hi", 2)` changes:

```
 inode bitmap      bit 25 set                       ialloc
 group descriptor  free_inodes_count--              ialloc
 superblock        free_inodes_count--              ialloc
 inode table       inode 26: mode, times            ialloc + iupdate
 /tmp's data       new entry "a" -> 26              dirlink
 block bitmap      bit 1234 set                     balloc (via bmap(.., alloc=1))
 group descriptor  free_blocks_count--              balloc
 superblock        free_blocks_count--              balloc
 block 1235        "hi"                             writei
 inode table       inode 26: size 2, block[0], i_blocks 2, mtime   writei → iupdate
```

`e2fsck`'s five passes check exactly these relations: every block in use
is marked in the bitmap and vice versa, the counts match the bitmaps,
each inode's `links_count` equals the number of directory entries naming
it, every directory is reachable, `i_blocks` matches the blocks really
used...

**Crash consistency** is out of scope: if the machine dies in the middle,
the structures disagree and `e2fsck` must repair them (that's why it
exists; journaling filesystems like ext3 solve it). We write each change
through to the disk immediately, which keeps the reasoning simple.

## 2. Allocating: bitmaps ([`super.c`](code/kernel/fs/super.c))

Each group has a **block bitmap** (bit *i* = block `first_data_block +
group * blocks_per_group + i` is used) and an **inode bitmap** (bit *i* =
inode `group * inodes_per_group + i + 1`).

```c
uint32_t balloc(dev_t dev)
{
    for (each group g with free blocks) {
        int bit = bitmap_take(groups[g].block_bitmap, sb.blocks_per_group);
        if (bit < 0)
            continue;
        groups[g].free_blocks_count--;      /* keep the counters in sync */
        sb.free_blocks_count--;
        write_counts(g);                    /* superblock + group descriptor */

        uint32_t blockno = sb.first_data_block + g * sb.blocks_per_group + bit;
        zero the block;                     /* never leak old data into a new file */
        return blockno;
    }
    return 0;                               /* disk full */
}
```

`ialloc(dev, mode)` is the same with the inode bitmap, plus it
initializes the inode (mode, times, everything else 0) and counts
directories (`used_dirs_count`, which `e2fsck` checks too). `bfree` and
`ifree` clear the bit and fix the counters.

Writing the superblock back: we only know the first part of the 1024-byte
structure. Read the block, `memcpy` our part over it, write it: the fields
we don't understand keep their values.

## 3. Writing file data ([`inode.c`](code/kernel/fs/inode.c))

**`bmap` gets an `alloc` flag.** Reading a hole returns 0. Writing into a
hole must allocate the data block, and maybe the pointer blocks on the
way to it:

```c
static uint32_t indirect(struct inode *ip, uint32_t blockno, uint32_t index, int alloc)
{
    if (blockno == 0)
        return 0;
    struct buf *b = bread(ip->dev, blockno);
    uint32_t *ptr = (uint32_t *)b->data;
    if (ptr[index] == 0 && alloc) {
        ptr[index] = alloc_for(ip);         /* balloc, and ip->d.blocks += 2 */
        bwrite(b);
    }
    ...
}
```

The structure of `bmap` doesn't change at all: the double-indirect case is
still `indirect(ip, indirect(ip, direct(ip, 13, alloc), fb / 256, alloc), fb % 256, alloc)`.

**`i_blocks` is in 512-byte units** and counts pointer blocks too:
a 13-block file uses 14 blocks (the 13th needs the single indirect block),
so `i_blocks = 28`. `e2fsck` checks it.

**`writei`** is `readi` backwards: for each block piece, `bmap(.., 1)`,
`bread`, `memcpy` in, `bwrite`. Then grow `size` if we wrote past the end,
set `mtime`, and `iupdate` (write the inode back to its inode table).

**`itrunc`** frees everything: direct blocks, then the pointer trees
(recursively: depth 1, 2, 3), and sets size and `i_blocks` to 0.
Careful: for device files, `block[0]` holds the device number, not a block!

## 4. Names and links

A file's **link count** is the number of directory entries pointing at it.
This is the V7 model, and V7's system calls map onto it directly:

| Call | Directory change | Link count |
|---|---|---|
| `creat(path, mode)` new file | `dirlink(parent, name, ip)` | 1 |
| `creat` on an existing file | none: `itrunc` the file | unchanged |
| `mknod(path, mode, dev)` | `dirlink` | 1 |
| `link(old, new)` | `dirlink(parent(new), name, ip)` | +1 |
| `unlink(path)` | `dirunlink(parent, name)` | −1 |

### When is a file deleted?

**Not** in `unlink`. A file is deleted when it has no names **and** nobody
is using it. That's `iput`'s job:

```c
void iput(struct inode *ip)
{
    if (ip->refcnt == 1 && ip->d.links_count == 0) {
        itrunc(ip);                 /* free the data */
        ip->d.mode = 0;
        ip->d.dtime = time_now();
        iupdate(ip);
        ifree(ip->dev, ip->inum, was_dir);
    }
    ip->refcnt--;
}
```

So a program can `unlink` its temporary file right after opening it; the
data lives until it `close`s (`fstest` checks this). The same mechanism
makes `mv` safe and lets you delete a running program's file.

### Directory entries ([`dir.c`](code/kernel/fs/dir.c))

**Adding** (`dirlink`): an entry needs `8 + name_len` bytes rounded up to
4. Look for an entry with that much **slack** (its `rec_len` minus what
it actually uses) and split it:

```
 before: [ inode=12 "bin" ........... rec_len=1000 ........... ]
 after:  [ inode=12 "bin" rec_len=12 ][ inode=40 "new" rec_len=988 ]
```

An unused entry (`inode == 0`) that is big enough is reused whole. If no
block has room, add a block to the directory holding just the new entry
(`rec_len = 1024`) and grow the directory's size by a block.

**Removing** (`dirunlink`): merge the entry into the previous one
(`prev->rec_len += de->rec_len`), or, if it's the first in its block, set
its `inode` to 0. The space is reused by later `dirlink`s.

Both are built on the same walker as `dirlookup`, now generalized: the
callback gets the buffer (to modify and `bwrite`) and the previous entry
(to merge into).

**`namei_parent(path, &name, &len)`** stops one component early and returns
the directory where `name` lives (or will live). `creat`, `mknod`, `link`'s
second argument and `unlink` all need it. In the reference, `namei` and
`namei_parent` share one function, `walk(path, parent, ...)`.

### Directories: no `mkdir` system call

V7 has no `mkdir` or `rmdir` system calls. The `mkdir` **program** (a
setuid-root program in V7) does it with `mknod` and `link`
([`mkdir.c`](code/user/bin/mkdir.c), after V7's):

```c
mknod("d", 040777, 0);      /* an empty directory inode, linked as "d"    links: 1 */
link("d", "d/.");           /* "." points at itself                       links: 2 */
link(".", "d/..");          /* ".." points at the parent; parent's links +1 */
```

So the kernel only needs `mknod` to accept a directory type, and `link`
to allow linking directories (V7: only for root; everybody is root here).
`rmdir` is the reverse: check the directory is empty, `unlink("d/.")`,
`unlink("d/..")`, `unlink("d")`. **The kernel doesn't check** that you
don't unlink a non-empty directory (V7 didn't either, root was trusted):
doing so orphans its contents, and `e2fsck` will put them in `lost+found`.

> **Why this matters for compatibility:** this is the API V7 programs use.
> If you add a `mkdir` syscall later (4.2BSD did), keep `mknod`+`link`
> working too.

## 5. Time ([`rtc.c`](code/kernel/dev/rtc.c))

Timestamps need a clock. The PC's battery-backed **CMOS clock** is
read through ports 0x70 (register number) and 0x71 (value): seconds,
minutes, hours, day, month, year, usually in BCD (0x59 = 59). We read it
once at boot, convert to seconds since 1970 (UTC), and then add timer
ticks: `time_now() = boot_time + ticks / HZ`. `time()` is V7 system call
13.

## 6. sync

The buffer cache writes through, and so does `iupdate`. So `sync` (V7
system call 36) has nothing to do. A write-back cache, the real
optimization, would mark buffers dirty and write them on `sync`, on
eviction, and every 30 seconds (V7's `update` daemon called `sync`).

---

## 7. Modules

| File | Responsibility | Exposes |
|---|---|---|
| [`fs/super.c`](code/kernel/fs/super.c) | bitmaps and counters | `balloc bfree ialloc ifree` |
| [`fs/inode.c`](code/kernel/fs/inode.c) | inode write-back, data | `iupdate bmap(.., alloc) writei itrunc inode_set_rdev`; `iput` deletes |
| [`fs/dir.c`](code/kernel/fs/dir.c) | entries | `dirlink dirunlink namei_parent` |
| [`fs/file.c`](code/kernel/fs/file.c) | `file_write` → `writei` | |
| [`sys/sys_file.c`](code/kernel/sys/sys_file.c) | `creat mknod link unlink chmod chown sync time` | |
| [`dev/rtc.c`](code/kernel/dev/rtc.c) | wall clock | `rtc_init time_now` |

## 8. Test

- The milestone; `e2fsck -fn build/disk.img` after **every** test run.
  Make a habit of it: it finds bugs your own tests never will.
- `debugfs -R "stat /tmp/d" build/disk.img`: compare links, size, blocks.
- Fill the disk: write a file until `write` fails with `ENOSPC`, delete
  it, check `e2fsck` and that the free count is back where it was.
- Create 100 files in one directory (forces `dirlink` to add blocks),
  delete every other one, create 50 more (reuses the slack), `e2fsck`.
- Note: `make` rebuilds `build/disk.img` whenever a program or `root/`
  changes, which throws away what toyos wrote. To keep a disk, copy it.

## Pitfalls

- **Forgetting a counter**: the bitmap is right but `free_blocks_count` in
  the superblock or the group descriptor isn't. `e2fsck` pass 5 says
  "Free blocks count wrong".
- **`i_blocks` wrong**: forgot the pointer blocks, or counted in 1 KiB
  units. `e2fsck` pass 1: "i_blocks is X, should be Y".
- **Not zeroing new blocks**: a new pointer block full of garbage points
  everywhere; a new data block leaks old file contents.
- **Freeing in `unlink`** instead of in `iput`: a program with the file
  open reads blocks now owned by someone else.
- **Overwriting the whole superblock** with your partial struct: fields
  past the end become 0, and `e2fsck` complains about a bad superblock.
- **Recursion on the kernel stack**: the kernel stack is 4 KiB. Freeing a
  pointer tree with a 1 KiB local array per level overflows it. Read the
  pointers from the cached buffer instead.
- **Device files in `itrunc`**: `block[0]` is the device number. "Freeing"
  it frees a random block.

## References

- "The Second Extended File System": <https://www.nongnu.org/ext2-doc/ext2.html>
  (bitmaps, directory entry rules, `i_blocks`).
- V7: `usr/sys/sys/alloc.c` (`alloc`, `free`, `ialloc`, `ifree`),
  `iget.c` (`iput`, `iupdat`, `itrunc`), `rdwri.c` (`writei`), `nami.c`,
  `sys4.c` (`link`, `unlink`, `chmod`, `chown`), `sys2.c` (`creat`, `mknod`),
  `usr/src/cmd/mkdir.c`, `rmdir.c`.
- Lions' Commentary, chapters 20–21.
- OSDev wiki: "CMOS" (the clock registers).
