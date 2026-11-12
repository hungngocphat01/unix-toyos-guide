# 21 — A Simple Shell

**Goal:** toyos becomes usable: `init` starts a **shell** on the console,
and you type commands. Run programs found through `$PATH`, redirect with
`<`, `>`, `>>`, chain with `;`, quote with `'` `"` `\`, `cd`, `exit`,
run scripts. This is all user space: no kernel changes.

**Milestone** (type it yourself with `make run-nox`):

```
Welcome to toyos!                <- /etc/rc ran "cat /etc/motd"
$ echo hello > /tmp/x; echo  world   >> /tmp/x
$ cat < /tmp/x
hello
world
$ echo 'a  b' "c ; d" e\;f
a  b c ; d e;f
$ cd /tmp; ls
.:
   26  040755   1024  .
    2  040755   1024  ..
   29  100666     12  x
$ nosuch
nosuch: not found
$ crash
...
Memory fault
$ sh -c 'echo one; echo two'
one
two
$ exit 3
init: shell exited, starting another one
$
```

**Reference code:** [`code/`](code/). New: [`user/bin/sh.c`](code/user/bin/sh.c),
[`user/bin/echo.c`](code/user/bin/echo.c), [`root/etc/rc`](code/root/etc/rc),
[`user/libc/sleep.c`](code/user/libc/sleep.c) (a stand-in). Rewritten:
[`user/bin/init.c`](code/user/bin/init.c). Removed: the test programs of
lessons 18–20 (`keys`, `fstest`, `libctest`; still in those lessons' code).

---

## 1. A shell is a loop

```
for (;;) {
    print a prompt
    read a line
    parse it into commands
    for each command:
        fork
        child:  set up stdin/stdout, exec the program
        parent: wait for the child
}
```

Everything you know as "the command line" is this loop. Programs don't
know about the shell, and the kernel doesn't either: `ls` just inherits
fds 0, 1, 2 and writes to fd 1.

### Why `fork` and `exec` are two calls

Between `fork` and `exec`, the child is **still running the shell's
code, but in its own process**. That gap is where the shell sets up the
new program's environment without any special kernel support:

```c
if (pid == 0) {                                 /* the child */
    if (c->out)
        redirect(1, c->out, c->append);         /* stdout -> the file */
    execvp(argv[0], argv);                      /* fds survive exec */
    ...
}
```

and `redirect` is just:

```c
f = creat(file, 0666);
close(1);           /* fd 1 is free now                        */
dup(f);             /* dup returns the LOWEST free fd: 1         */
close(f);
```

V7 has no `dup2`, no `O_APPEND`, no `posix_spawn`: the lowest-fd rule of
lesson 17, and `lseek(f, 0L, 2)` for `>>`, are all it takes. `exec`
replaces the memory but keeps the open files, so the program starts with
its stdout already redirected.

### Why `cd` and `exit` are built in

`cd` as a program would change *its own* current directory, then exit:
the shell's wouldn't move. Anything that changes the shell's own state
must run inside the shell process. (Same for `exit`, and later `set`,
variable assignment, `wait`...)

### `exit` vs `_exit` in the child

If `exec` fails, the child must stop. With `exit()`, stdio would flush
**the child's copy** of the parent's buffers (lesson 20): output the
parent printed but hadn't flushed would come out twice. The child uses
`_exit()`.

## 2. Parsing ([`sh.c`](code/user/bin/sh.c))

Two layers:

**The lexer** turns characters into tokens: `WORD`, `;`, `<`, `>`, `>>`,
end. A word ends at a blank or a special character, *unless quoted*:

| Quoting | Example | Word |
|---|---|---|
| `'...'` | `'a  b'` | `a  b` |
| `"..."` | `"c ; d"` | `c ; d` (in V7's sh, `$` still expands inside) |
| `\` | `e\;f` | `e;f` |

Quotes are removed while copying the word into a separate buffer
(`words[]`), so `argv` points at clean strings.

**The parser** collects a command's words into `argv` and its redirection
targets into `in`/`out`, until `;` or the end of the line:

```c
struct cmd {
    char *argv[MAXARGS + 1];
    char *in;                   /* < file, or NULL */
    char *out;                  /* > file, or NULL */
    int   append;               /* 1 if >> */
};
```

Note that redirections can be anywhere: `> out echo hi` works.

This grammar grows in lessons 23 (`&`) and 25 (`|`) by the same pattern: a
pipeline is a list of `struct cmd`.

## 3. Finding programs: `$PATH`

`execvp` (V7's, in libc) tries each directory of `$PATH`
(`:/bin:/usr/bin`; the empty first entry means the current directory).

And if a file is executable but not ELF? The kernel's `exec` fails with
`ENOEXEC`, and **`execvp` runs `/bin/sh file args` instead**. That's
how shell scripts work in V7: no `#!` line (that came in 1980). Try it
once `chmod` exists (lesson 22).

Where does `$PATH` come from? `init` passes an environment
(`HOME=/`, `PATH=:/bin:/usr/bin`) to the shell with `execve`, and every
program inherits it through `execv`, which passes `environ` along.

## 4. init

[`init.c`](code/user/bin/init.c) is process 1, and has three jobs:

1. **Open the console** as fds 0, 1, 2 (every other process inherits
   them).
2. **Start the system**: run `/etc/rc` with the shell (V7: mount
   filesystems, clean `/tmp`, start daemons. Ours: print the motd).
3. **Keep a shell running** on the console: when it exits (`exit`, or ^D),
   start another.

And a fourth, implicit one: **reap orphans**. When a process dies, its
children are re-parented to init (lesson 13); their exit statuses must be
collected with `wait`, or they stay zombies forever. init's `wait` loop
collects whatever exits.

V7's real init went further: after a single-user shell exits, it reads
`/etc/ttys` and starts a `getty` on each terminal, which prints `login:`
and `exec`s `login`, which checks `/etc/passwd`, sets the uid and `exec`s
the user's shell. A chain of `exec`s, no special kernel support.

> **Note: there is no `&` yet.** With our sequential `fork` (lesson 13),
> the parent can't run until the child exits, so background jobs are
> impossible. They arrive with the scheduler in lessons 23–24.

## 5. Reporting how commands died

`wait` gives the V7 status: `exit(n)` → `n << 8`; killed by signal `s`
→ `s` (plus `0200` if a core was dumped). The shell prints V7's
messages for signals (`Memory fault` for `SIGSEGV`, `Illegal instruction`,
`Floating exception`...) and keeps the exit code for `exit` with no
argument.

---

## 6. Modules

| File | Responsibility |
|---|---|
| [`user/bin/sh.c`](code/user/bin/sh.c) | lexer, parser, `run` (fork / redirect / exec / wait), built-ins |
| [`user/bin/init.c`](code/user/bin/init.c) | console, `/etc/rc`, respawn the shell, reap orphans |
| [`user/bin/echo.c`](code/user/bin/echo.c) | `echo` |
| [`root/etc/rc`](code/root/etc/rc) | startup script |
| [`user/libc/sleep.c`](code/user/libc/sleep.c) | temporary `sleep` (`execvp` needs one) |

## 7. Test

Interactively, or scripted over the serial port:

```sh
(sleep 2; printf 'echo hi > /tmp/a\r'; sleep .4; printf 'cat /tmp/a\r'; sleep .5) \
    | timeout 5 make run-test
```

- Every row of the milestone.
- Errors: `cat < /nonexistent` (cannot open), `echo 'oops` (unterminated
  quote), `cat <` (file name expected), `cd /etc/motd` (bad directory).
- `> out echo hi` (redirection first).
- A shell script: `echo 'echo from a script' > /tmp/s; sh /tmp/s`.
- ^D at the prompt: init starts a new shell.
- `crash`, then check the shell still works (the fault killed only the child).

## Pitfalls

- **`dup` instead of `close` + `dup`**: the new fd isn't 1.
- **Redirecting in the parent**: the shell's own stdout goes to the file,
  and stays there.
- **`exit()` in the child after a failed `exec`**: double output.
- **Waiting for the wrong child**: `wait` returns *any* child, so loop
  until you get the pid you started.
- **Prompt on stdout**: `ls > file` would capture... no, the prompt is
  printed by the shell, not `ls`, but `sh script > out` would put prompts
  in `out`. Prompt only when interactive (`isatty(0)`), on stderr, as V7.

## References

- V7: `usr/src/cmd/sh/` (the Bourne shell: `main.c`, `cmd.c` parser,
  `xec.c` execution, `io.c` redirection), `usr/src/cmd/init.c`,
  `usr/src/libc/gen/execvp.c`, `usr/man/man1/sh.1`.
- K. Thompson, "The UNIX Command Language" (1976), and D. M. Ritchie,
  "The Evolution of the Unix Time-sharing System" (1979): why `fork` and
  `exec` are separate.
- xv6: `sh.c` (a different parser: a tree of commands).
