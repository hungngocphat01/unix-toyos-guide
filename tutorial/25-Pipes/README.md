# 25 — Pipes

**Goal:** `ls /bin | wc -l`. A **pipe** is a one-way channel between
processes: a buffer in the kernel, written at one end, read at the other.
With sleep/wakeup (lesson 24) it takes about 100 lines. Then: `|` in the
shell, `dup2`, `popen` and `system` in libc, and V7's `find`.

**Milestone:**

```
$ ls /bin | wc -l
     49
$ cat /etc/passwd | sort | tr a-z A-Z
BIN::3:1::/BIN:
KEN::100:10:KEN THOMPSON:/USR/KEN:/BIN/SH
ROOT::0:1:THE SUPERUSER:/:/BIN/SH
$ ls -l /bin | sort +3n | tail -2
-rwxr-xr-x 1 root   106744 Nov 11 15:12 find
-rwxr-xr-x 1 root   113768 Nov 11 15:12 ed
$ cat /bin/sort /bin/ed | sum               <- 213 KB through a 4 KB pipe
58218   428
$ yes | dd count=1 | wc -c                  <- yes is stopped when dd quits
1+0 records in
1+0 records out
    512
$ find / -type d -print
/
/lost+found
/bin
...
$ find /bin -perm -4000 -print
/bin/mkdir
/bin/mv
...
```

**Reference code:** [`code/`](code/). New:
[`fs/pipe.c`](code/kernel/fs/pipe.c), [`fs/pipe.h`](code/kernel/fs/pipe.h),
V7's [`popen.c`](code/user/libc/v7/popen.c) and
[`system.c`](code/user/libc/v7/system.c),
[`v7bin/find.c`](code/user/v7bin/find.c). Changed:
[`fs/file.c`](code/kernel/fs/file.c) and [`file.h`](code/kernel/fs/file.h)
(a file is an inode *or* a pipe), [`sys/sys_file.c`](code/kernel/sys/sys_file.c)
(`pipe`, `dup2`, `EPIPE`), [`fs/dir.c`](code/kernel/fs/dir.c)
(`dir_size_v7`), [`abi/syscall.h`](code/abi/syscall.h) (`DUPFLG`),
[`user/libc/syscall.c`](code/user/libc/syscall.c) (`dup2`),
[`user/bin/sh.c`](code/user/bin/sh.c) (`|`), `stdio.h` and `stdlib.h`.

---

## 1. What a pipe is

```c
int fd[2];
pipe(fd);           /* fd[0]: read end, fd[1]: write end */
```

Bytes written to `fd[1]` come out of `fd[0]`, in order. Alone, that's
useless: the trick is `fork`. Both processes inherit both ends; each
closes the end it doesn't use, and now the parent and the child (or two
children) talk:

```
     ls                                wc
  fd 1 ──► [ write end ]══ pipe ══[ read end ] ──► fd 0
```

Neither program knows: `ls` writes to fd 1, `wc` reads fd 0, exactly as
with a terminal or a file. That's the Unix idea in one picture: small
programs composed by the shell.

### The kernel side ([`pipe.c`](code/kernel/fs/pipe.c))

A ring buffer of one page, like V7's 4096-byte `PIPSIZ`:

```c
struct pipe {
    char     *data;                 /* PIPESIZE bytes */
    uint32_t  r, w;                 /* free-running: w - r bytes are waiting */
    int       reader_open, writer_open;
    uint32_t  id;
};
```

The rules, all four of them:

| Situation | read | write |
|---|---|---|
| data / room available | take what's there (maybe less than asked) | put it all |
| empty / full | **sleep** until a writer adds data | **sleep** until a reader makes room |
| empty, and **no writer left** | return 0: **end of file** | |
| **no reader left** | | fail with `EPIPE` (and `SIGPIPE`, §4) |

```c
int pipe_read(struct pipe *p, void *buf, uint32_t n)
{
    while (p->r == p->w) {          /* empty */
        if (!p->writer_open)
            return 0;               /* and will stay empty: end of file */
        sleep(&p->r);
    }
    while (got < n && p->r != p->w)
        dst[got++] = p->data[p->r++ % PIPESIZE];
    wakeup(&p->w);                  /* there's room now */
    return got;
}
```

`pipe_write` is the mirror image, byte by byte: when full, wake the
reader and sleep on `&p->w`. Two channels, `&p->r` ("data arrived") and
`&p->w` ("room appeared"), are just two addresses inside the pipe.

**End of file is about the writers**, which is why closing matters so
much. `wc` stops only when *every* write end is closed. If the shell
kept a copy of the write end, `wc` would wait forever.

### Plugged into the file layer

`struct file` gets a second kind of object:

```c
struct file {
    int           refcnt;
    int           mode;             /* FREAD for the read end, FWRITE for the write end */
    uint32_t      offset;
    struct inode *ip;               /* exactly one of ip and pipe is set */
    struct pipe  *pipe;
};
```

`file_read`/`file_write` hand pipes to `pipe_read`/`pipe_write`;
`file_close` calls `pipe_close(p, writer?)` on the last reference
(which wakes the other side: maybe it's waiting for EOF); the pipe is freed
when both ends are closed. `lseek` on a pipe is `ESPIPE`, `ioctl` is
`ENOTTY`.

`fork` and `dup` need **nothing new**: they share the `struct file`, so the
pipe end is open until the last process holding it closes it or exits.

`pipe()` returns two numbers: the read fd in EAX, the write fd in **EDX**,
as planned in lesson 22.

> **V7 did it differently.** A V7 pipe was an **inode** on the root
> filesystem, with no name, read and written through the normal file code
> with two offsets. Its data lived in the buffer cache, and on disk if
> needed. Ours is a page of kernel memory: simpler, and it never touches
> the disk.

## 2. `fstat` on a pipe: identity matters

`fstat` on a pipe has no inode to describe, so it reports a regular file
of the size waiting in it, as V7 did. The device and inode number are made
up, but they **must be different for each pipe**. V7's `cat` refuses to
copy a file onto itself:

```c
if (statb.st_dev == dev && statb.st_ino == ino)
    fprintf(stderr, "cat: input %s is output\n", ...);
```

With every pipe reporting `(0, 0)`, `x | cat | cat` failed: stdin and
stdout looked like the same file. Each pipe now gets a fresh number
(`st_dev = PIPEDEV`, `st_ino = pipe_id(p)`).

## 3. The shell: `a | b | c`

The parser collects commands separated by `|` into an array, a
**pipeline**. Running it ([`sh.c`](code/user/bin/sh.c)):

```c
int in = -1;                        /* read end from the previous command */
for (int i = 0; i < n; i++) {
    int fd[2] = { -1, -1 };
    if (i < n - 1)
        pipe(fd);                   /* our output goes to the next command */
    pids[i] = start(&c[i], in, fd[1], fd[0]);
    if (in >= 0)                    /* the shell keeps NO pipe ends */
        close(in);
    if (fd[1] >= 0)
        close(fd[1]);
    in = fd[0];
}
```

and in the child, `start` moves the ends into place with the lowest-fd
rule (lesson 21):

```c
if (in >= 0)  { close(0); dup(in);  close(in);  }   /* stdin  = previous pipe */
if (out >= 0) { close(1); dup(out); close(out); }   /* stdout = next pipe */
close(unused);                       /* the read end of our own output pipe */
```

All commands run **at the same time**: the shell starts them all, then
waits for all of them, and reports the status of the last one.

Count the closes. Every pipe end must end up in exactly one process.
Forget one in the shell, and the last reader never sees end of file:
the pipeline hangs.

## 4. Nobody reading: EPIPE and SIGPIPE

`yes | dd count=1`: `dd` reads one block and exits. `yes` writes forever,
and V7's `yes` never checks for errors. Each write now fails with `EPIPE`,
which `yes` ignores, so it would spin forever and the shell would wait
for it forever.

That's what **SIGPIPE** is for: a process writing to a pipe without
readers is **killed** by default. Signals arrive in lesson 26; until then
`sys_write` applies the default action itself:

```c
int r = file_write(f, buf, n);
if (r == -EPIPE)
    proc_exit(SIGPIPE);     /* SIGPIPE's default action; lesson 26 sends a real signal */
```

The shell prints nothing for SIGPIPE: it's a normal way for a pipeline
to end.

## 5. `dup2`, V7 style

V7's `popen` (below) uses `dup2(fd, fd2)`: "make `fd2` a copy of `fd`,
closing `fd2` first". V7 didn't add a system call for it. It reused
`dup`, with a flag in the first argument
([`sys_dup`](code/kernel/sys/sys_file.c)):

```c
#define DUPFLG 0100                 /* abi/syscall.h */

int dup2(int fd, int fd2)           /* libc */
{
    return syscall3(SYS_dup, fd | DUPFLG, fd2, 0);
}
```

The kernel strips the flag; if it was set, it uses `fd2` instead of the
lowest free fd. (V7's Bourne shell relies on it too.)

## 6. popen and system

Two V7 libc functions, unchanged, now that pipes exist:

```c
FILE *f = popen("ls /etc | sort -r", "r");     /* read its output */
FILE *f = popen("tr a-z A-Z", "w");            /* write its input */
pclose(f);                                     /* wait for it; its status */
system("echo hi; exit 3");                     /* run, wait: 3 << 8 */
```

Both run `/bin/sh -c cmd`; `popen` connects a pipe between your `FILE *`
and the command's stdin or stdout. (They also set `SIGINT` to ignore while
waiting. Our stand-in `signal()` accepts that silently until lesson 26.)

## 7. `find`, and the size of a directory

V7's `find` compiles as is, but walked garbage at first. It reads
directories like this:

```c
dirsize = Statb.st_size;
for (offset = 0; offset < dirsize; offset += 512) {
    dsize = min(512, dirsize - offset);
    read(dir, dentry, dsize);
    for (dp = dentry, entries = dsize >> 4; entries; --entries, ++dp)
        ...                         /* 16 bytes per entry */
```

It trusts `st_size` for the number of entries and **ignores what `read`
returns**. Our `read` gives V7 16-byte entries (lesson 17), but `stat`
reported the **ext2** size (1024 bytes per block). So `find` believed
in 64 entries per block, and the 58 after the real ones were leftovers
from the previous directory.

The fix is making the translation consistent: `stat` of a directory now
reports the size of the **V7 view**, 16 bytes per entry
([`dir_size_v7`](code/kernel/fs/dir.c)). `ls -ld /etc` says 96 for its 6
entries, as V7's would.

---

## 8. Modules

| File | Responsibility | Exposes |
|---|---|---|
| [`fs/pipe.c`](code/kernel/fs/pipe.c) | ring buffer, sleep/wakeup, EOF and EPIPE | `pipe_alloc pipe_close pipe_read pipe_write pipe_count pipe_id` |
| [`fs/file.c`](code/kernel/fs/file.c) | inode or pipe | `file_open_pipe stat_file` |
| [`sys/sys_file.c`](code/kernel/sys/sys_file.c) | `pipe`, `dup` with `DUPFLG`, SIGPIPE's default | `sys_pipe` |
| [`fs/dir.c`](code/kernel/fs/dir.c) | directory size in the V7 view | `dir_size_v7` |
| [`user/bin/sh.c`](code/user/bin/sh.c) | pipelines | |
| [`user/libc/v7/popen.c`](code/user/libc/v7/popen.c), [`system.c`](code/user/libc/v7/system.c) | run a command | `popen pclose system` |

## 9. Test

- The milestone.
- More data than fits: `cat /bin/sort /bin/ed | sum` against
  `cat /bin/sort /bin/ed > /tmp/x; sum /tmp/x`. Same checksum.
- A long pipeline: `echo x | cat | cat | cat | cat | cat | cat | cat`.
- ^P while `cat | cat` waits for input: the second `cat` sleeps in
  `pipe_read`, the first in `tty_read`.
- Errors: `echo hi |`, `| wc` (syntax), `yes | dd count=1` (no hang).
- `ls /bin | wc -l & wait`.
- `e2fsck -fn build/disk.img`.

## Pitfalls

- **An extra write end left open** (in the shell, or in the reader
  itself): the reader never gets end of file. The number one pipe bug.
- **`if` instead of `while` around `sleep`**: another reader may have
  emptied the pipe first.
- **Not waking the other side in `pipe_close`**: a reader sleeping on an
  empty pipe sleeps forever after the last writer exits.
- **Waiting for the first command before starting the second**: if the
  first writes more than 4 KiB, it fills the pipe and sleeps, waiting
  for a reader that doesn't exist yet. Deadlock.
- **Freeing the pipe when one end closes**: the other end still uses it.

## References

- V7: `usr/sys/sys/pipe.c` (`pipe`, `readp`, `writep`, `plock`),
  `usr/sys/sys/sys4.c` (`dup` with the `0100` flag),
  `usr/src/libc/stdio/popen.c` and `system.c`,
  `usr/src/cmd/find.c`, `usr/src/cmd/sh/xec.c` (pipelines in the Bourne shell).
- xv6: `pipe.c`, `sh.c` (`PIPE` commands).
- M. D. McIlroy's 1964 memo ("we should have some ways of coupling
  programs like garden hose"), and D. M. Ritchie, "The Evolution of the
  Unix Time-sharing System": how pipes arrived in 1973, in one night.
