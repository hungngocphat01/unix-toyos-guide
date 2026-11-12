# 20 — User-Space C Library

**Goal:** replace our toy libc with **V7's own C library**: `stdio`
(`fopen`, `fprintf`, `fgets`, `scanf`, buffering), `malloc`, `qsort`,
`ctime`, `getpwuid`, `getenv`, `perror`, `setjmp`... compiled from the
1979 sources. Only the machine-dependent part is rewritten. That's what
"source compatible with V7" means in practice: V7 code compiles and runs.

No kernel changes in this lesson.

**Milestone:**

```
== printf
[   42] [42   ] [00042] [-7]
[ff] [10] [3000000000] [A] [str] [tru] [   right]
[3.141590] [0.67] [1.234568e+04] [.0001] [1.e+20]
sprintf returns its buffer: 7-up
== files
sum of squares read back with fscanf: 55
first line with fgets: 1 squared is 1
fopen /nonexistent: No such file or directory
== malloc, qsort
apple banana cherry fig pear
malloc: 50 blocks and a 20000-byte one at 4120dc (sbrk(0) = 417330)
== strings and numbers
atoi("  -123") = -123, atof("2.5e3") = 2500
sscanf("hex ff", "%s %x") -> "hex" 255
isdigit('7') 1, isalpha('7') 0, toupper('q') Q
== time, users, environment
ctime: Wed Nov 11 14:45:40 2026
getpwuid(0): root, home /, shell /bin/sh
getenv("PATH") = :/bin:/usr/bin
ttyname(0) = /dev/console
== setjmp
setjmp returned 42 after longjmp
```

(`.0001` and `1.e+20` are what V7's `%g` really printed.)

**Reference code:** [`code/`](code/). New:
[`user/libc/v7/`](code/user/libc/v7/) (65 files from V7, see its
[`README`](code/user/libc/v7/README)),
[`doprnt.c`](code/user/libc/doprnt.c), [`printf.c`](code/user/libc/printf.c),
[`scanf.c`](code/user/libc/scanf.c), [`setjmp.asm`](code/user/libc/setjmp.asm),
[`fpmath.c`](code/user/libc/fpmath.c), [`ftime.c`](code/user/libc/ftime.c),
[`abort.c`](code/user/libc/abort.c), headers in
[`user/libc/include/`](code/user/libc/include/) (notably
[`sys/cdefs.h`](code/user/libc/include/sys/cdefs.h) and
[`stdio.h`](code/user/libc/include/stdio.h)),
[`root/etc/passwd`](code/root/etc/passwd), [`root/etc/group`](code/root/etc/group),
[`libctest.c`](code/user/bin/libctest.c).
Changed: the [`Makefile`](code/Makefile) (two C dialects, `.DEFAULT_GOAL`),
[`user.ld`](code/user/user.ld) (aligned `end`),
[`syscall.c`](code/user/libc/syscall.c) (`execv execve execl execle`),
[`exit.c`](code/user/libc/exit.c) (flushes stdio).
Removed: our `printf.c`, `tty.c`, most of `string.c`.

---

## 1. What a C library is made of

Sort V7's ~100 libc source files by "does this depend on the machine or
the kernel?":

| Kind | Examples | What we do |
|---|---|---|
| Pure C algorithms | `strcpy`, `atoi`, `qsort`, `malloc`, `ctime`, `crypt` | compile V7's file |
| C on top of system calls | all of `stdio`, `getpwent`, `ttyname`, `perror`, `execvp` | compile V7's file |
| System call stubs | `read`, `open`, `fork`... (`sys/*.s`) | ours since lesson 11 |
| Startup | `crt0.s` | ours since lesson 12 |
| CPU-specific | `setjmp`, `_doprnt`, `ldexp`/`frexp`/`modf` | rewrite |

Only the bottom three rows are PDP-11 assembly. Everything else is
portable C, the point of writing Unix in C in the first place. Of the 65
V7 files we compile, **9 needed a change**, each marked `toyos:`.

## 2. Compiling 1979 C with a 2026 compiler

V7 C (pre-ANSI "K&R" C) differs from today's in ways that matter:

```c
/* V7's strcpy.c */
char *
strcpy(s1, s2)                  /* parameter types declared below */
register char *s1, *s2;
{
    register char *os1;         /* "register" was a real hint then */
    os1 = s1;
    while (*s1++ = *s2++)
        ;
    return(os1);
}
```

- **Old-style definitions** and no prototypes; implicit `int` (`main(argc, argv)`).
- **Calling undeclared functions** (implicitly `int f()`).
- **Declaring library functions yourself**: `char *malloc();`,
  `char *sprintf();` (no `<stdlib.h>` or `<string.h>` in V7).
- **Tentative definitions everywhere**: `int errno;` in several files.

GCC 14+ rejects implicit declarations and implicit int by default, **but
not in `-std=gnu89` mode**. So the [`Makefile`](code/Makefile) has two
sets of flags:

```make
USER_CFLAGS := -std=gnu11 -Wall -Wextra ... $(USER_COMMON)   # our code
V7_CFLAGS   := -std=gnu89 -w -fcommon $(USER_COMMON)         # V7 code
```

`-fcommon` restores the old linker behavior where `int errno;` in several
files is one variable (GCC 10 changed the default).

The fixes that were still needed, every one a "1979 C vs modern C" issue:

| Problem | Where | Fix |
|---|---|---|
| `f()` called before `static f(...)` is defined: the call implicitly declared it `extern` | `ctime ecvt execvp getenv qsort endopen` | add `static` forward declarations |
| `static char x[] { ... }` (initializer without `=`) | `crypt.c` | add `=` |
| string literals are read-only now; `ungetc` wrote into `sscanf`'s string | `ungetc.c` | don't write back for strings |
| `char *sys_errlist[];` (a definition) vs the real array | `perror.c` | `extern` |
| PDP-11 float limits (`LOGHUGE` 39) | `<math.h>` | IEEE double's (308) |

`grep -n toyos user/libc/v7/*.c` lists them.

## 3. One set of headers for two kinds of C

Our programs (modern C) want prototypes; V7 code declares things like
`char *sprintf();` itself, and **a K&R declaration conflicts with a
prototype that has `...`**. 4.4BSD solved this with a macro, and so do we
([`sys/cdefs.h`](code/user/libc/include/sys/cdefs.h)):

```c
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#define __P(args) args          /* modern C: a full prototype  */
#else
#define __P(args) ()            /* gnu89 (V7 code): K&R style  */
#endif

char *sprintf __P((char *, const char *, ...));
```

`-std=gnu89` doesn't define `__STDC_VERSION__`, so V7 code sees
`char *sprintf();`, compatible with its own declarations.

Keep V7's types where V7 code depends on them: `malloc` returns `char *`
(V7 had no `void *`), `sprintf` returns its buffer, `ino_t` is 16 bits so
`struct direct` stays 16 bytes. The header set:

| Header | Origin |
|---|---|
| `stdio.h ctype.h setjmp.h time.h pwd.h grp.h math.h sgtty.h sys/types.h sys/stat.h sys/dir.h sys/timeb.h errno.h` | V7's, with `__P` declarations |
| `string.h stdlib.h unistd.h` | not in V7: for modern code (V7 programs ignore them) |
| `sys/cdefs.h` | 4.4BSD's idea |

## 4. stdio: how buffering works

`FILE` is V7's ([`stdio.h`](code/user/libc/include/stdio.h)):

```c
struct _iobuf {
    char *_ptr;     /* next character in the buffer             */
    int   _cnt;     /* characters left to read / room to write   */
    char *_base;    /* the buffer (BUFSIZ = 512, malloc'ed)       */
    char  _flag;    /* _IOREAD _IOWRT _IONBF _IOEOF _IOERR ...    */
    char  _file;    /* the file descriptor                       */
};

#define getc(p)    (--(p)->_cnt >= 0 ? *(p)->_ptr++ & 0377 : _filbuf(p))
#define putc(x, p) (--(p)->_cnt >= 0 ? (*(p)->_ptr++ = (x)) : _flsbuf((x), p))
```

`getc` and `putc` are **macros**: usually just a pointer increment. Only
when the buffer is empty (`_filbuf`: one `read` of 512 bytes) or full
(`_flsbuf`: one `write`) do they call the kernel. That's the whole point
of stdio: reading a file character by character costs one system call
per 512 characters.

Three V7 details that will surprise you:

- **stdout on a terminal is unbuffered** (`_flsbuf` checks `isatty`), not
  line-buffered as today: every character is a `write`. Slow but simple.
  Into a file or (lesson 25) a pipe, it's buffered.
- **stderr is always unbuffered** (`data.c`).
- **`exit()` flushes**: [`exit.c`](code/user/libc/exit.c) calls
  `_cleanup()`, which `fclose`s everything. `_exit()` doesn't: a child
  after `fork` that fails to `exec` must use `_exit`, or it flushes a copy
  of the parent's buffer and the output appears twice.

`sprintf`/`sscanf` use a fake `FILE` with the `_IOSTRG` flag whose buffer
is the string: the same formatting code serves files and strings.

## 5. The rewritten parts

### `_doprnt` (the printf engine)

V7's is PDP-11 assembly, and `printf` called it with `&args`, the address
of the first variable argument, assuming the rest follow it in memory.
The portable way is `<stdarg.h>`:

```c
int printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = _doprnt(fmt, ap, stdout);
    va_end(ap);
    return r;
}
```

[`doprnt.c`](code/user/libc/doprnt.c) implements V7's conversions: `%d %u
%o %x %c %s %e %f %g`, flags `-` and `0`, width and precision (both may
be `*`), and `%D %O %X` = long. Floating point uses V7's own `ecvt`,
`fcvt` (digits of a number) and `gcvt` (shortest form, for `%g`).

`_doscan` (scanf) is V7's C. It takes the arguments as an `int **`
array; on i386 a `va_list` *is* a pointer into the stack's array of
argument slots, so [`scanf.c`](code/user/libc/scanf.c) passes it cast.
(This would not work on x86-64, where arguments travel in registers.)

### `setjmp`/`longjmp`

`setjmp(buf)` saves what's needed to come back to the point right after
the call: the callee-saved registers (`ebx esi edi ebp`), the stack
pointer and the return address. `longjmp(buf, v)` restores them and jumps
to that return address with `eax = v`: `setjmp` "returns again". V7's `ed`
uses it to abandon a command on error (and, from lesson 26, on ^C).
[`setjmp.asm`](code/user/libc/setjmp.asm) is 25 instructions.

### `frexp`, `ldexp`, `modf`

`atof` and `ecvt` take numbers apart: `frexp` splits a double into
mantissa and exponent, `ldexp` puts them back, `modf` splits integer and
fraction. With the IEEE 754 layout (1 sign bit, 11 exponent bits, 52
fraction bits) each is a few bit operations ([`fpmath.c`](code/user/libc/fpmath.c)).

> **Floating point and the kernel.** User programs now use the x87 FPU.
> The kernel never touches it, and only one process runs at a time, so
> nothing needs saving yet. Lesson 24 (preemption)
> must save and restore FPU state on each switch (`fxsave`/`fxrstor`).

### Small ones

- `ftime()`, which `ctime` calls for the time zone, was a V7 system call.
  We have no time zones, so it's a libc function on top of `time()`: UTC.
- `exec`: V7's names. `execv(path, argv)` passes `environ`;
  `execve(path, argv, envp)` is system call 59; `execl`/`execle` collect
  their arguments with `va_arg`; `execvp` (V7's C) searches `$PATH`.

## 6. Bugs this lesson flushed out

Porting real code finds real assumptions:

- **`malloc` corrupted its arena**: V7's `malloc` marks busy blocks by
  setting **bit 0 of pointers**, so the heap must be word-aligned. Our
  `end` symbol (start of the heap) was wherever `.bss` ended, an odd
  address. Fix: `. = ALIGN(16); end = .;` in [`user.ld`](code/user/user.ld).
- **`sscanf` on a literal faulted**: `ungetc` writes the character back
  into the buffer, and literals live in read-only pages now (V7 had none).

## 7. Modules

| File | Responsibility |
|---|---|
| [`user/libc/v7/*.c`](code/user/libc/v7/) | V7's stdio and gen (portable C) |
| [`user/libc/doprnt.c`](code/user/libc/doprnt.c), [`printf.c`](code/user/libc/printf.c), [`scanf.c`](code/user/libc/scanf.c) | formatted I/O entry points |
| [`user/libc/syscall.c`](code/user/libc/syscall.c) | system calls, exec family |
| [`user/libc/setjmp.asm`](code/user/libc/setjmp.asm), [`fpmath.c`](code/user/libc/fpmath.c), [`abort.c`](code/user/libc/abort.c), [`exit.c`](code/user/libc/exit.c), [`sbrk.c`](code/user/libc/sbrk.c), [`ftime.c`](code/user/libc/ftime.c), [`string.c`](code/user/libc/string.c) | the machine-dependent rest |
| [`user/libc/include/`](code/user/libc/include/) | headers usable from K&R and modern C |

## 8. Test

- The milestone ([`libctest.c`](code/user/bin/libctest.c)).
- Write a file with `fprintf` in a loop of 10 000 lines, count the
  `write` system calls (add a counter in `sys_write`): about 10 000 × 20 / 512.
- `printf` without `exit` flushing: return from `main` while writing to a
  file; the last partial buffer must still be there.
- Run `fork()` in a program whose stdout is a file, after a `printf`
  without `\n`: see the line appear twice. Then `fflush(stdout)` before
  `fork`. (Classic stdio trap.)

## Pitfalls

- **`-std=gnu11` for V7 files**: hundreds of errors. Use gnu89.
- **Prototypes with `...` in the headers without `__P`**: "conflicting
  types for 'sprintf'" in every V7 file declaring it.
- **Changing CFLAGS doesn't rebuild objects**: `make clean`.
- **Heap not aligned**: random `malloc` crashes (see §6).
- **Missing `memcpy`**: GCC emits calls to it for struct copies even with
  `-ffreestanding`.

## References

- V7: `usr/src/libc/` (all of it), `usr/include/stdio.h`, and the manual,
  section 3: `usr/man/man3/` (`stdio.3`, `printf.3s`, `malloc.3`...).
- Kernighan & Ritchie, *The C Programming Language*, 1st ed. (1978): the
  dialect V7 is written in; its chapter 8 is a small stdio.
- P. J. Plauger, *The Standard C Library* (1992): what ANSI changed.
- 4.4BSD `sys/cdefs.h`: the `__P` macro.
