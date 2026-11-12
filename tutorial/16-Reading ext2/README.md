# 16 — Reading ext2

**Goal:** find files by path on a real ext2 disk and read their contents,
using V7's kernel vocabulary: `iget`, `iput`, `bmap`, `readi`, `namei`.

**Milestone:**

```
ext2: 8192 blocks, 2048 inodes, 1 group(s), 7871 free blocks
ls /: .(2) ..(2) lost+found(11) bin(12) dev(16) etc(17) tmp(19)
/etc/motd (18 bytes): Welcome to toyos!
/bin/hello: inode 14, 18276 bytes, mode 100755
/nope: not found
```

**Reference code:** [`code/`](code/). New:
[`fs/ext2.h`](code/kernel/fs/ext2.h) (disk format),
[`fs/fs.h`](code/kernel/fs/fs.h) (the layer's interface),
[`fs/super.c`](code/kernel/fs/super.c), [`fs/inode.c`](code/kernel/fs/inode.c),
[`fs/dir.c`](code/kernel/fs/dir.c), [`abi/types.h`](code/abi/types.h),
[`abi/stat.h`](code/abi/stat.h), [`tools/mkdisk.sh`](code/tools/mkdisk.sh),
[`root/`](code/root/) (files copied onto the disk). Changed: the
[`Makefile`](code/Makefile) builds the image with `mkdisk.sh`; `%o` in `kprintf`;
`fs_test()` in [`kmain.c`](code/kernel/kmain.c).

---

## 1. A filesystem is a data structure on a disk

ext2 is a direct descendant of the V7 filesystem: the same three ideas,
with bigger numbers.

1. **Inodes.** Each file is described by a fixed-size record, its *inode*:
   type, permissions, owner, size, times, link count, and **where its data
   blocks are**. Inodes are numbered and stored in an array on disk (the
   *inode table*). **An inode has no name.**
2. **Directories are files** whose content is a list of `(name, inode
   number)` pairs. A name exists only in a directory entry, which is why one
   file can have several names (hard links, lesson 19).
3. **Free space** is tracked in bitmaps: one for blocks, one for inodes
   (needed only for writing, lesson 19).

```
 path "/etc/motd"
   │
   ▼
 inode 2 (root dir, always 2) ──data──► [ "."→2  ".."→2  "bin"→12  "etc"→17 ... ]
                                                                     │
 inode 17 (etc) ─────────────────data──► [ "."→17 ".."→2  "motd"→18 ]
                                                              │
 inode 18 (motd): size=18, block[0]=1234 ──data──► block 1234: "Welcome to toyos!\n"
```

### The disk layout

With 1 KiB blocks:

```
 block 0   boot block (unused)
 block 1   superblock: magic 0xEF53, block size, counts, inodes/blocks per group
 block 2   group descriptor table: for each group, where its bitmaps and inode table are
 block 3+  group 0: block bitmap | inode bitmap | inode table | data blocks ...
           group 1: ...  (only one group on our 8 MiB disk)
```

ext2 splits the disk into **block groups** (8192 blocks each with 1 KiB
blocks) so that a file's inode and data can be close together. For us it
only matters in one formula, "where is inode N?":

```c
group = (N - 1) / inodes_per_group;           /* inode numbers start at 1 */
index = (N - 1) % inodes_per_group;
block = groups[group].inode_table + index * inode_size / BLOCK_SIZE;
offset_in_block = index * inode_size % BLOCK_SIZE;
```

That's `ext2_inode_block()` in [`super.c`](code/kernel/fs/super.c).

### Where is a file's data? `bmap`

An inode has 15 block pointers. The first 12 point at data directly. The
13th points at a block **full of pointers** (256 of them, 4 bytes each), the
14th at a block of pointers to blocks of pointers, and so on:

```
 i_block[0..11] ───────────────────────────────► data     (12 KiB)
 i_block[12]    ──► [256 ptrs] ──────────────────► data     (256 KiB)
 i_block[13]    ──► [256 ptrs] ──► [256 ptrs] ───► data     (64 MiB)
 i_block[14]    ──► [256] ──► [256] ──► [256] ───► data     (16 GiB)
```

`bmap(ip, n)` answers "which disk block holds block `n` of this file?".
Same structure as V7's (which had 10 direct pointers). A pointer of 0
means a **hole**: the block was never written and reads as zeros.

### Directories

An ext2 directory block is a chain of variable-length entries:

```
 offset 0    inode=2   rec_len=12  name_len=1 type=2 "."
 offset 12   inode=2   rec_len=12  name_len=2 type=2 ".."
 offset 24   inode=11  rec_len=20  name_len=10 type=2 "lost+found"
 offset 44   inode=12  rec_len=12  name_len=3 type=2 "bin"
 ...
 last entry: rec_len stretches to the end of the block
```

`rec_len` says where the next entry is; names are **not** NUL-terminated.
An entry with `inode == 0` is unused. Entries never cross a block, so
`dirlookup` reads the directory block by block and walks each chain
([`dir.c`](code/kernel/fs/dir.c)).

(V7 directories were much simpler: fixed 16-byte entries, 2-byte inode +
14-byte name. V7 programs that read directories expect that format, so
lesson 17 translates.)

### `namei`: path → inode

Start at the root inode (always inode 2), and for each component, look it
up in the current directory:

```c
struct inode *ip = iget(rootdev, EXT2_ROOT_INO);
while (there is another component) {
    struct inode *next = dirlookup(ip, name, len);
    iput(ip);
    if (!next)
        return NULL;
    ip = next;
}
return ip;
```

## 2. The in-memory inode table

Reading an inode from disk every time would be slow, and worse: if two
processes have the same file open, they must see **the same** size and
block list as it changes (lesson 19). So like the buffer cache, there's at
most **one `struct inode` per file** in memory ([`inode.c`](code/kernel/fs/inode.c)):

- `iget(dev, inum)`: find it in `inodes[]` (refcnt++), or load it into a
  free slot.
- `iput(ip)`: refcnt--. At 0, the slot can be reused. (Lesson 19: if the
  file was also deleted, *that's* when its blocks are freed.)
- `idup(ip)`: another reference to the same inode (for `fork`, `chdir`...).

Same discipline as buffers: **every `iget` (or `namei`, which returns an
`iget`ed inode) needs one `iput`**.

The layering is now:

```
 namei ──► dirlookup ──► readi/bmap ──► bread ──► bdevsw ──► ata
             iget ───────────────────► bread
```

## 3. Making the disk image: `tools/mkdisk.sh`

```sh
mke2fs -q -t ext2 -b 1024 -I 128 \
       -O ^dir_index,^ext_attr,^resize_inode,^sparse_super,^large_file \
       -E root_owner=0:0 -d build/root build/disk.img 8M
```

- `-d build/root` copies a directory tree into the new filesystem (no
  root privileges or mounting needed). The script stages `root/` from the
  repo plus the programs in `/bin`.
- `-b 1024 -I 128` and turning features off gives the plainest ext2:
  1 KiB blocks (the kernel checks), 128-byte inodes, no hashed directories.
  The one feature left on, `filetype`, adds the type byte to directory entries.
- Then `debugfs` sets every file's owner to root (the files on your Mac
  belong to you, uid 501).

**`debugfs` is your best debugging friend** for this and lesson 19:

```sh
D=/opt/homebrew/opt/e2fsprogs/sbin
$D/debugfs -R "ls -l /bin" build/disk.img
$D/debugfs -R "stat /bin/hello" build/disk.img     # inode fields, block list
$D/debugfs -R "blocks /bin/hello" build/disk.img
$D/dumpe2fs build/disk.img | less                  # superblock, groups
```

Compare what your kernel prints with what `debugfs` says.

---

## 4. Modules

| File | Responsibility | Exposes |
|---|---|---|
| [`fs/ext2.h`](code/kernel/fs/ext2.h) | on-disk structs | `ext2_superblock ext2_group_desc ext2_inode ext2_dirent` |
| [`fs/super.c`](code/kernel/fs/super.c) | mount: superblock, group descriptors; locate inodes | `ext2_mount_root`, `rootdev`, `ext2_inode_block` |
| [`fs/inode.c`](code/kernel/fs/inode.c) | inode table, data blocks | `iget idup iput bmap readi` |
| [`fs/dir.c`](code/kernel/fs/dir.c) | directories, paths | `dirlookup namei` |
| [`abi/stat.h`](code/abi/stat.h) | `S_IF*` (same bits as ext2's `i_mode`), V7 `struct stat` | |
| [`abi/types.h`](code/abi/types.h) | `ino_t dev_t off_t time_t` | |
| [`tools/mkdisk.sh`](code/tools/mkdisk.sh) | build the image | |

`exec` still loads from boot modules: next lesson switches it to `namei`.

## 5. Test

- The milestone, cross-checked with `debugfs -R "ls -l /"`.
- Put a 300 KiB file in `root/` (`dd if=/dev/urandom of=root/big bs=1k count=300`),
  read it all with `readi` and compute a simple checksum; compare with a
  checksum on the host. 300 KiB needs the double-indirect block.
- `namei("/etc/motd/x")` → NULL (motd isn't a directory).
- `namei("//etc///motd")` works (repeated slashes).

## Pitfalls

- **`inode numbers start at 1`**: forgetting the `- 1` reads the wrong inode,
  usually one that looks almost right.
- **`i_blocks` counts 512-byte sectors**, not blocks. Use `size` for the length.
- **Names aren't NUL-terminated** in directory entries. Compare with
  `name_len`.
- **Infinite loop on `rec_len == 0`** in a corrupted (or wrongly-read)
  directory block. Guard against it.
- **Leaking inodes in `namei`**: every early `return` must `iput` the
  current directory.
- **The superblock is at byte 1024**, which is block 1 only for 1 KiB
  blocks. With 4 KiB blocks it's inside block 0.

## References

- "The Second Extended File System" (Dave Poirier): <https://www.nongnu.org/ext2-doc/ext2.html>,
  the definitive description.
- V7: `usr/sys/sys/iget.c` (`iget`, `iput`), `subr.c` (`bmap`), `rdwri.c` (`readi`),
  `nami.c` (`namei`), `usr/sys/h/inode.h`, `ino.h`, `filsys.h`.
- Lions' Commentary, chapters 18–19.
- xv6: `fs.c` (same functions, xv6's own simpler format).
