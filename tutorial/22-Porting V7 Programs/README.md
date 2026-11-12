# 22 — Porting V7 Programs

**Goal:** compile **44 of V7's own commands** from their 1979 source and
run them on toyos: `ls cat cp mv rm ln mkdir rmdir pwd ed grep sort wc od
tail tr cal date su passwd`... For that the kernel learns **who you are**
(user and group ids, real and effective, setuid programs) and **V7's
permission rules**, plus the handful of system calls these programs use.

**Milestone:**

```
$ ls -l /
total 34
drwxr-xr-x 2 root     1024 Nov 11 14:55 bin
...
drwxrwxrwx 2 root     1024 Nov 11 14:55 tmp
$ cal 11 2026
   November 2026
 S  M Tu  W Th  F  S
 1  2  3  4  5  6  7
...
$ sort -t: +2n /etc/passwd
root::0:1:The Superuser:/:/bin/sh
bin::3:1::/bin:
ken::100:10:Ken Thompson:/usr/ken:/bin/sh
$ su ken
$ echo hi > /mine
sh: cannot create /mine
$ cd /usr/ken; echo secret > mine; chmod 600 mine; exit
$ su bin
$ cat /usr/ken/mine
cat: can't open /usr/ken/mine
$ exit
$ passwd ken
New password:
Retype new password:
$ su bin
$ su ken
Password:
$ ed /tmp/notes
?/tmp/notes
a
first line
.
1s/first/1st/p
1st line
w
9
q
```

**Reference code:** [`code/`](code/). New:
[`user/v7bin/`](code/user/v7bin/) (44 V7 programs),
[`kernel/fs/perm.c`](code/kernel/fs/perm.c),
[`<signal.h>`](code/user/libc/include/signal.h) with a stand-in
[`signal.c`](code/user/libc/signal.c),
[`<sys/param.h>`](code/user/libc/include/sys/param.h),
[`<utmp.h>`](code/user/libc/include/utmp.h), V7's
`getpass.c getlogin.c ttyslot.c` in [`user/libc/v7/`](code/user/libc/v7/).
Changed: [`proc/proc.h`](code/kernel/proc/proc.h) (ids, umask),
[`proc/exec.c`](code/kernel/proc/exec.c) (setuid),
[`fs/dir.c`](code/kernel/fs/dir.c) (`namei` reports why it failed, checks
search permission), [`sys/sys_file.c`](code/kernel/sys/sys_file.c) (checks,
`access utime stime`), [`sys/sys_proc.c`](code/kernel/sys/sys_proc.c)
(`getuid setuid getgid setgid umask`), [`abi/syscall.h`](code/abi/syscall.h)
(second return value), [`user/libc/syscall.c`](code/user/libc/syscall.c),
[`sgtty.h`](code/user/libc/include/sgtty.h) (all of V7's constants),
[`tools/mkdisk.sh`](code/tools/mkdisk.sh) (setuid programs, `/usr/ken`),
the [`Makefile`](code/Makefile). Removed: our `cat ls echo mkdir`.

---

## 1. Porting, measured

The method: compile every candidate, link it, and read the errors. They
tell you exactly what's missing. Against lesson 21's libc, 44 programs
produced only this:

| Missing | Wanted by | Provided by |
|---|---|---|
| `<sys/param.h>` | `ls pwd grep du` | a user version: `BSIZE NOFILE HZ ROOTINO`... |
| `<signal.h>`, `signal()` | `mkdir mv tee sort su passwd ed`... | V7's header; a stand-in `signal()` (§5) |
| `<utmp.h>` | `date who` | V7's struct |
| `access` `umask` `getuid`... | `rm rmdir chmod su passwd` | system calls (§3, §4) |
| `stime` | `date` (setting the clock) | system call 25 |
| `getpass` `getlogin` | `su passwd` | V7's libc files |
| `pipe` | `ed` (`!` command) | a wrapper; the call arrives in lesson 25 |
| `B9600 TIOCHPCL CR1`... | `stty` | the rest of V7's `<sgtty.h>` |

And **one program needed a source change**: `ed` (§6). The rest
compile untouched with `-std=gnu89`; the Makefile builds `user/v7bin/*.c`
with `V7_CFLAGS` and installs them in `/bin` next to ours.

That's the payoff of choosing V7's ABI and file formats in lessons
11–20: V7's own programs are the test suite.

## 2. Users and groups

Every process carries **who it runs as** ([`proc.h`](code/kernel/proc/proc.h)):

```c
int uid, gid;       /* effective: what permissions are checked against */
int ruid, rgid;     /* real: who started it                           */
int umask;          /* bits creat/mknod turn off                       */
```

Everything is inherited by `fork` and kept by `exec`. init starts as root
(uid 0). New files belong to the effective uid and gid of their creator.

### Permission bits

Each inode has an owner, a group and 9 bits: `rwx` for the owner, the
group, everybody else. **Exactly one triple applies**, never a
combination ([`perm.c`](code/kernel/fs/perm.c), V7's `access()`):

```c
int iaccess(struct inode *ip, int mode)     /* mode: 0400, 0200 or 0100 */
{
    if (current->uid == 0) {                /* the superuser... */
        if (mode == S_IEXEC && !IS_DIR(ip) && !(ip->d.mode & 0111))
            return -EACCES;                 /* ...can't run a non-program */
        return 0;
    }
    if (current->uid != ip->d.uid) {
        mode >>= 3;                         /* not the owner: group bits */
        if (current->gid != ip->d.gid)
            mode >>= 3;                     /* not the group: other bits */
    }
    return (ip->d.mode & mode) ? 0 : -EACCES;
}
```

(So a file with mode `0077` is unreadable by its owner and readable by
everyone else. Strange, but that's the rule.)

### Where the kernel checks (V7's rules)

| Operation | Needs |
|---|---|
| look up a path | `x` (search) on every directory on the way: in `namei` |
| `open` for reading / writing | `r` / `w` on the file |
| `creat` a new file, `mknod`, `link` | `w` on the directory (adding a name = writing the directory) |
| `creat` an existing file | `w` on the file |
| `unlink` | `w` on the directory (**not** on the file!) |
| `exec` | `x` on the file, which must be regular |
| `chdir` | `x` on the directory |
| `chmod`, `utime` | being the owner (or root) |
| `chown`, `mknod`, `link`/`unlink` a directory, `stime` | root |

Note `unlink`: removing a name modifies the directory, not the file. You
can delete a read-only file in your own directory, and you can't delete
your own file from a directory you can't write. V7's `rm` asks first when
the file itself is read-only, but that's `rm`'s politeness, not the
kernel's rule.

`namei` used to just return NULL. Now that a lookup can fail for several
reasons, it reports which:

```c
struct inode *namei(const char *path, int *err);   /* -ENOENT -ENOTDIR -EACCES */
```

### setuid programs

How does a normal user change their password, when `/etc/passwd` is
writable only by root? The `passwd` program is owned by root and has the
**setuid bit** (`04000`). `exec` of such a file sets the process's
**effective** uid to the file's owner:

```c
if (mode & S_ISUID)
    p->uid = owner;         /* the real uid (ruid) stays: who you are */
if (mode & S_ISGID)
    p->gid = group;
```

So `passwd` runs with root's permissions but knows who called it (the
real uid), and only lets you change *your* password. In V7, `mkdir`,
`rmdir` and `mv` are setuid root too, because only root may `mknod` a
directory or `link`/`unlink` one (lesson 19).
[`mkdisk.sh`](code/tools/mkdisk.sh) installs them `04755`.

Related system calls ([`sys_proc.c`](code/kernel/sys/sys_proc.c)):

- `setuid(uid)`: root may become anyone (that's `su` and `login`); others
  may only go back to their real uid.
- `access(path, mode)`: "could the **real** user do this?" A setuid
  program checks on behalf of its caller. (The kernel swaps in the real
  ids for the duration of the check.)
- `umask(mask)`: `creat("f", 0666)` with umask `022` makes `0644`.

## 3. Two return values: `getuid`

V7's `getuid` returns the real uid in r0 **and** the effective one in r1;
libc's `geteuid()` is the same system call, reading r1. Our ABI does the
same with EDX ([`abi/syscall.h`](code/abi/syscall.h)):

```c
int32_t sys_getuid(struct trapframe *tf)
{
    tf->edx = current->uid;         /* second result: effective uid */
    return current->ruid;           /* first result (EAX): real uid */
}
```

The dispatcher only writes EAX, so EDX reaches user space as the handler
left it. In libc, an inline-asm variant of `syscall3` also returns EDX.
`pipe` (lesson 25) will return its two file descriptors this way.

## 4. Smaller kernel additions

- `utime(path, times)`: set access/modification times (owner or root).
- `stime(t)`: set the clock (root). V7's syntax is `date [yymmdd]hhmm`:
  `date 1200` makes it noon today. (Two-digit years: V7 wasn't ready for 2026.)
- `chown` by root only; `chmod` by the owner.

## 5. `signal()` before signals

Many V7 programs start with `signal(SIGINT, SIG_IGN)` (so ^C doesn't
interrupt them halfway) or install a handler to clean up temporary files.
Signals arrive in lesson 26. Until then libc's `signal()` remembers
nothing and returns `SIG_DFL`. That's correct as long as no signal is ever
delivered, which is true today.

## 6. Bugs that porting found

Real programs exercise paths our own tests never did:

**`brk` and `sbrk` must share the break.** `sort` calls `brk()` directly
to claim 32 KiB after its data. Our `sbrk` kept its own copy of the
break, starting at `end`. When stdio later called `malloc` → `sbrk`, it
computed `end + 1024` and called `brk` with it: **shrinking** the heap
under `sort`'s feet, then a page fault. In V7 both functions live in
`sbrk.s` and share one variable; ours now live together in
[`syscall.c`](code/user/libc/syscall.c).

**String literals are read-only now.** `ed` does
`mktemp("/tmp/eXXXXX")`, and `mktemp` overwrites the X's. In 1979 that
was fine (no read-only memory). Today the literal is in a read-only page:
page fault. Find it with the faulting `eip` and
`i686-elf-addr2line -e build/user/bin/ed <eip>`; fix with an array:

```c
{   /* toyos: mktemp rewrites its argument; literals are read-only now */
    static char tmpl[] = "/tmp/eXXXXX";
    tfname = mktemp(tmpl);
}
```

**`ar` never removes members.** Deleting `sbrk.c` left the old `sbrk.o`
inside `libc.a`: "multiple definition of sbrk". The Makefile now
recreates the archive from scratch.

## 7. What's not ported, and why

| Program | Needs |
|---|---|
| `sh` (the Bourne shell) | its 1979 C: struct members shared between structs, pointers used as ints (lesson 26 explains) |
| `kill`, `sleep` done right | signals: lesson 26 |
| `find` (`popen`), `ed`'s `!` | pipes: lesson 25 |
| `login`, `getty`, `who` | multi-user init and `/etc/utmp`: a good exercise |
| `ps`, `pstat` | read kernel memory through `/dev/kmem` and the kernel's symbol table |
| `df`, `fsck`, `mkfs`, `dump` | the V7 disk format (we use ext2) |
| `cc`, `as`, `ld` | a PDP-11 code generator, and a.out |

---

## 8. Modules

| File | Responsibility |
|---|---|
| [`kernel/fs/perm.c`](code/kernel/fs/perm.c) | `iaccess iowner suser` |
| [`kernel/fs/dir.c`](code/kernel/fs/dir.c) | search permission and error codes in `namei` |
| [`kernel/proc/exec.c`](code/kernel/proc/exec.c) | setuid/setgid |
| [`kernel/sys/sys_file.c`](code/kernel/sys/sys_file.c) | permission checks; `access utime stime` |
| [`kernel/sys/sys_proc.c`](code/kernel/sys/sys_proc.c) | `getuid getgid setuid setgid umask` |
| [`user/v7bin/`](code/user/v7bin/) | V7 commands |
| [`user/libc/`](code/user/libc/) | new wrappers, `signal` stand-in, headers |

## 9. Test

- The milestone, and the permission table row by row (as `ken` and `bin`).
- `ls -l` in `/bin`: setuid programs show `s` (`-rwsr-xr-x`).
- `cd /usr/ken` as `bin` after `chmod 700 /usr/ken`: "bad directory".
- `umask` (via a tiny program) then `echo > f; ls -l f`.
- `e2fsck -fn build/disk.img` after `passwd` (it renames files in `/etc`).
- Run each program in `/bin` once with no arguments. None should crash.

## Pitfalls

- **Checking write permission on the file in `unlink`**: wrong; it's the
  directory.
- **Root and execute**: root may read and write anything, but not execute
  a file with no `x` bit at all.
- **Using the effective uid in `access()`**: the whole point is the real uid.
- **New files owned by root**: set `uid`/`gid` from the creating process.
- **`setuid` letting anybody become anybody**: only root, or back to the
  real uid.
- **Leaking a reference in an error path**: every check you add between
  `namei` and the end needs an `iput` on failure. `e2fsck` won't see
  this, but `iget: no free inodes` will, eventually.

## References

- V7: `usr/sys/sys/fio.c` (`access`, `owner`, `suser`),
  `usr/sys/sys/sys4.c` (`getuid`, `setuid`, `chmod`, `chown`, `umask`,
  `utime`, `stime`), `usr/sys/sys/sys1.c` (setuid in `exec`),
  `usr/src/cmd/*.c`, `usr/src/libc/sys/getuid.s`.
- D. M. Ritchie, "On the Security of UNIX" (V7 manual, vol. 2).
- D. M. Ritchie, US patent 4,135,240 (1979): the setuid bit.
