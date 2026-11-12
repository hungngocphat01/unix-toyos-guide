# 17 — Files and File Descriptors

**Goal:** the Unix file API: `open`, `read`, `write`, `close`, `lseek`,
`dup`, `stat`, `fstat`, `chdir`. Programs now live on the disk (`exec`
uses `namei`), the console is a **file** (`/dev/console`), and V7 programs
can read directories the way they expect.

**Milestone:**

```
starting /bin/init as pid 1
init: console open as fd 0, 1, 2

init: running /bin/cat /etc/motd
Welcome to toyos!

init: running /bin/ls /
/:
    2  040755   1024  .
    2  040755   1024  ..
   11  040700  12288  lost+found
   12  040755   1024  bin
   17  040755   1024  dev
   ...
init: running /bin/ls /dev
/dev:
   21  020622      0  console
   22  020666      0  null

init: running /bin/cat motd
Welcome to toyos!

init: after lseek(fd, 11, 0): "toyos!
init: write to a file: errno 30 (EROFS = 30, until lesson 19)
...
```

**Reference code:** [`code/`](code/). New: [`fs/file.h`](code/kernel/fs/file.h),
[`fs/file.c`](code/kernel/fs/file.c), [`abi/dir.h`](code/abi/dir.h),
user [`cat.c`](code/user/bin/cat.c) and [`ls.c`](code/user/bin/ls.c),
`sys/*.h` headers in [`user/libc/include/sys/`](code/user/libc/include/sys/).
Changed: [`sys/sys_file.c`](code/kernel/sys/sys_file.c) (all file syscalls),
[`dev/dev.c`](code/kernel/dev/dev.c) (`cdevsw`: console and null),
[`fs/dir.c`](code/kernel/fs/dir.c) (cwd, `dir_read_v7`),
[`fs/inode.c`](code/kernel/fs/inode.c) (`inode_rdev`),
[`proc/proc.c`](code/kernel/proc/proc.c) (fds and cwd: fork/exit),
[`proc/exec.c`](code/kernel/proc/exec.c) (loads from a file),
[`tools/mkdisk.sh`](code/tools/mkdisk.sh) (`/dev` nodes),
[`kmain.c`](code/kernel/kmain.c), the [`Makefile`](code/Makefile) (no more `-initrd`).
Removed: `bootmod.c`.

---

## 1. Three levels: fd → open file → inode

```
  process A                  system file table           inode table
  ofile[0] ──┐               ┌───────────────────┐
  ofile[1] ──┼─────────────► │ file: offset 0    │ ──┐
  ofile[2] ──┘   (dup)       │ mode R+W          │   │     ┌──────────────────┐
  ofile[3] ────────────────► │ file: offset 812  │ ──┼───► │ inode /dev/console│
                             │ mode R            │   │     └──────────────────┘
  process B (forked from A)  └───────────────────┘   │     ┌──────────────────┐
  ofile[0..2] ─────► same struct file as A's 0..2    └───► │ inode /etc/motd   │
                                                           └──────────────────┘
```

Why three levels, and not "fd → inode with an offset"?

- **The offset belongs to the open file, not to the fd or the inode.**
  After `fork`, parent and child share the offset of every inherited fd:
  if a shell script's commands all write to the same output file, each
  command continues where the previous one stopped. So `fork` and `dup`
  share one `struct file`.
- **Two `open()`s of the same file** get two `struct file`s (independent
  offsets) pointing at the same `struct inode` (one size, one block list).

That's V7's design exactly: `u.u_ofile[NOFILE]` → `file[NFILE]` → `inode[NINODE]`.

**Reference counts** at each level:

| Operation | per-process fd | `struct file` | `struct inode` |
|---|---|---|---|
| `open` | new fd | new, refcnt 1 | `namei` → refcnt++ |
| `dup`, `fork` | new fd(s) | refcnt++ | — |
| `close`, `exit` | fd freed | refcnt--; at 0: `iput` | refcnt-- |

**`fd_alloc` returns the lowest free fd.** Lots of Unix code depends on it:
`close(1); open("out", 1)` makes the file the new stdout (the shell's
`>` in lesson 21).

## 2. Device files: everything is a file

`/dev/console` is an inode with type `S_IFCHR` and a device number (stored
in the inode's block pointers, decoded by `inode_rdev`). `read`/`write` on
it go to the driver via the **character device switch**:

```c
int file_write(struct file *f, const void *buf, uint32_t n)
{
    struct inode *ip = f->ip;
    ...
    if ((ip->d.mode & S_IFMT) == S_IFCHR) {
        dev_t dev = inode_rdev(ip);
        return cdevsw[MAJOR(dev)].write(MINOR(dev), buf, n);
    }
    ...  /* regular file: writei, lesson 19 */
}
```

```c
struct cdevsw cdevsw[] = {
    [CDEV_CONSOLE] = { console_read, console_write },   /* /dev/console: c 0 0 */
    [CDEV_NULL]    = { null_read,    null_write },      /* /dev/null:    c 1 0 */
};
```

So a program writing to fd 1 doesn't know or care whether fd 1 is the
console, a file, or (lesson 25) a pipe. The kernel's old special case
"fd 1 = console" is gone.

**Who opens fd 0, 1, 2?** Nobody gives a process open files out of
nowhere. Like V7's `init`, our `init` opens `/dev/console` (fd 0) and
`dup`s it twice (fd 1, 2). Every other process inherits them through
`fork`. That's why everything "just has" stdin/stdout/stderr.

The device nodes are created on the image by `debugfs`
([`mkdisk.sh`](code/tools/mkdisk.sh)): `mknod console c 0 0`.

## 3. The current directory

`proc->cwd` is an inode reference. `namei` starts there for relative paths
(it starts at the root for paths beginning with `/`). `chdir` swaps it
(`iput` the old one), `fork` shares it (`idup`), `exit` releases it.

V7 had no `getcwd` system call: `pwd` finds its own path by walking `..`
and reading each parent directory to find the child's name. That only works
if directories can be `read()`, which brings us to...

## 4. Reading directories, the V7 way

V7 programs (`ls`, `pwd`, `find`, `rm -r`, the shell's `*` expansion) open
a directory and `read()` it as an array of 16-byte entries:

```c
struct direct {
    ino_t d_ino;                /* 2 bytes, 0 = unused */
    char  d_name[DIRSIZ];       /* 14 bytes, NUL-padded */
};
```

ext2 entries are variable-length. So the kernel **translates**: a `read`
on a directory inode returns V7 entries, where entry *i* (at offset `16·i`)
is the *i*-th used ext2 entry ([`dir_read_v7`](code/kernel/fs/dir.c)).
This is a choice you could make differently (it's recorded in `AGENTS.md`):

| Option | Pros | Cons |
|---|---|---|
| **Translate in `read()`** (reference) | V7 programs work unmodified | names cut at 14 chars; O(n²) for big directories; inode numbers must fit in 16 bits |
| Return raw ext2 entries | simple, honest | every V7 program that reads directories must be ported to a new API |
| Use the V7 filesystem format instead of ext2 | 100% faithful | no `mke2fs`/`debugfs`/`e2fsck` tooling on your Mac |

To keep both `dirlookup` and `dir_read_v7` short, [`dir.c`](code/kernel/fs/dir.c)
factors the walk over entries into `dir_foreach(dp, fn, arg)`.

## 5. exec from the disk

`exec` now does `namei(path)` and reads with `readi`. Because `load_elf`
only ever used `read_image()`, that's the only part that changed:

```c
static int read_image(struct image *img, uint32_t off, void *dst, uint32_t n)
{
    return readi(img->ip, dst, off, n) == (int)n ? 0 : -1;
}
```

It also checks the file is regular and has an execute bit (`-EACCES`
otherwise). The boot modules and `bootmod.c` are gone.

## 6. Permissions (not yet)

Everything runs as root (uid 0) and the kernel doesn't check permission
bits on `open`. V7's rules (owner/group/other, root bypasses everything
except execute) are a small function, `access(ip, mode)`, worth adding when
you implement `setuid`/`getuid` for `login` and `su` (lesson 22).

---

## 7. Modules

| File | Responsibility | Exposes |
|---|---|---|
| [`fs/file.c`](code/kernel/fs/file.c) | open file table, dispatch by inode type | `file_open file_dup file_close file_read file_write stat_inode` |
| [`sys/sys_file.c`](code/kernel/sys/sys_file.c) | fd table, syscalls | `sys_open close read write lseek dup stat fstat chdir` |
| [`dev/dev.c`](code/kernel/dev/dev.c) | `cdevsw[]`: console, null | |
| [`fs/dir.c`](code/kernel/fs/dir.c) | `namei` honors cwd; `dir_foreach`; `dir_read_v7` | |
| [`fs/inode.c`](code/kernel/fs/inode.c) | `inode_rdev` (device number of a device file) | |
| [`proc/proc.c`](code/kernel/proc/proc.c) | `ofile[]`, `cwd`: inherit on fork, release on exit | |
| [`abi/dir.h`](code/abi/dir.h) | V7 `struct direct` | |
| [`user/libc`](code/user/libc/) | `open close read lseek dup stat fstat chdir`, `<sys/stat.h>`, `<sys/dir.h>`, a printf with widths | |

## 8. Test

- The milestone.
- `dup` shares the offset: open motd, `dup` it, read 5 bytes from each fd:
  the second read continues where the first stopped.
- Two `open`s don't share: same test with two `open`s, both read "Welco".
- Open 20 files without closing: the 21st fails with `EMFILE`.
- `read` into a kernel address: `EFAULT`.
- `ls /bin`; `cat /bin/ls | wc`... no `wc` yet: write one!

## Pitfalls

- **Leaking a file reference** in an error path of `open` (forgot
  `file_close`/`iput`).
- **`fork` not incrementing file refcounts**: the child's `exit` closes the
  parent's console. Symptom: output stops after the first child exits.
- **`lseek` on a terminal**: meaningless; V7 didn't check, we return `ESPIPE`.
- **Offset in bytes vs entries** in `dir_read_v7`: the V7 view's offsets
  are multiples of 16, unrelated to ext2 byte offsets.
- **Forgetting that `namei` can be called with no current process** (during
  boot): the reference falls back to the root.

## References

- V7: `usr/sys/sys/sys2.c` (`read`, `write`, `open`, `close`, `seek`),
  `fio.c` (`getf`, `closef`, `falloc`, `ufalloc`), `sys3.c` (`stat`, `dup`),
  `usr/sys/h/file.h`, `usr/src/cmd/ls.c`, `usr/src/cmd/pwd.c`.
- Lions' Commentary, chapter 18 ("File Access and Control").
- xv6: `file.c`, `sysfile.c`.
