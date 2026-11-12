/* syscall.c -- one small C function per system call (V7: the .s files in libc/sys).
 * The ABI is described in abi/syscall.h. */
#include <errno.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include "abi/syscall.h"

int errno;
char **environ;

/* Make system call num with up to 3 arguments.
 * "=@ccc" asks GCC for the carry flag after the instruction. */
static int syscall3(int num, int a, int b, int c)
{
    int ret;
    char failed;

    __asm__ volatile ("int $0x80"
                      : "=a"(ret), "=@ccc"(failed)
                      : "a"(num), "b"(a), "c"(b), "d"(c)
                      : "memory");
    if (failed) {
        errno = ret;
        return -1;
    }
    return ret;
}

/* Same, for the calls that return a second value in EDX (abi/syscall.h). */
static int syscall3_edx(int num, int a, int *edx)
{
    int ret, second;
    char failed;

    __asm__ volatile ("int $0x80"
                      : "=a"(ret), "=d"(second), "=@ccc"(failed)
                      : "a"(num), "b"(a)
                      : "memory");
    *edx = second;
    if (failed) {
        errno = ret;
        return -1;
    }
    return ret;
}

void _exit(int status)
{
    syscall3(SYS_exit, status, 0, 0);
    for (;;)
        ;
}

int read(int fd, void *buf, unsigned n)
{
    return syscall3(SYS_read, fd, (int)buf, n);
}

int write(int fd, const void *buf, unsigned n)
{
    return syscall3(SYS_write, fd, (int)buf, n);
}

int open(const char *path, int how)
{
    return syscall3(SYS_open, (int)path, how, 0);
}

int close(int fd)
{
    return syscall3(SYS_close, fd, 0, 0);
}

long lseek(int fd, long offset, int whence)
{
    return syscall3(SYS_lseek, fd, offset, whence);
}

int dup(int fd)
{
    return syscall3(SYS_dup, fd, 0, 0);
}

int dup2(int fd, int fd2)
{
    return syscall3(SYS_dup, fd | DUPFLG, fd2, 0);
}

int stat(const char *path, struct stat *st)
{
    return syscall3(SYS_stat, (int)path, (int)st, 0);
}

int fstat(int fd, struct stat *st)
{
    return syscall3(SYS_fstat, fd, (int)st, 0);
}

int ioctl(int fd, int request, void *arg)
{
    return syscall3(SYS_ioctl, fd, request, (int)arg);
}

int creat(const char *path, int mode)
{
    return syscall3(SYS_creat, (int)path, mode, 0);
}

int mknod(const char *path, int mode, int dev)
{
    return syscall3(SYS_mknod, (int)path, mode, dev);
}

int link(const char *old, const char *new)
{
    return syscall3(SYS_link, (int)old, (int)new, 0);
}

int unlink(const char *path)
{
    return syscall3(SYS_unlink, (int)path, 0, 0);
}

int chmod(const char *path, int mode)
{
    return syscall3(SYS_chmod, (int)path, mode, 0);
}

int chown(const char *path, int uid, int gid)
{
    return syscall3(SYS_chown, (int)path, uid, gid);
}

void sync(void)
{
    syscall3(SYS_sync, 0, 0, 0);
}

time_t time(time_t *tloc)
{
    time_t t = syscall3(SYS_time, 0, 0, 0);
    if (tloc)
        *tloc = t;
    return t;
}

int chdir(const char *path)
{
    return syscall3(SYS_chdir, (int)path, 0, 0);
}

int fork(void)
{
    return syscall3(SYS_fork, 0, 0, 0);
}

int wait(int *status)
{
    return syscall3(SYS_wait, (int)status, 0, 0);
}

int getpid(void)
{
    return syscall3(SYS_getpid, 0, 0, 0);
}

int execve(const char *path, char **argv, char **envp)
{
    return syscall3(SYS_exece, (int)path, (int)argv, (int)envp);
}

int execv(const char *path, char **argv)
{
    return execve(path, argv, environ);
}

/* execl("/bin/ls", "ls", "-l", (char *)0): the arguments are already an
 * argv-shaped array on the stack, but va_arg is the portable way to see it. */
#define MAXARGS 64

int execl(const char *path, const char *arg0, ...)
{
    char *argv[MAXARGS + 1];
    va_list ap;
    int i = 0;

    va_start(ap, arg0);
    for (char *a = (char *)arg0; a && i < MAXARGS; a = va_arg(ap, char *))
        argv[i++] = a;
    va_end(ap);
    argv[i] = 0;
    return execv(path, argv);
}

/* execle(path, arg0, ..., (char *)0, envp) */
int execle(const char *path, const char *arg0, ...)
{
    char *argv[MAXARGS + 1];
    va_list ap;
    int i = 0;

    va_start(ap, arg0);
    for (char *a = (char *)arg0; a && i < MAXARGS; a = va_arg(ap, char *))
        argv[i++] = a;
    argv[i] = 0;
    char **envp = va_arg(ap, char **);
    va_end(ap);
    return execve(path, argv, envp);
}

/* The current break (end of the heap). brk and sbrk must share it: a
 * program may call brk() itself (V7's sort does), and the next sbrk()
 * (from malloc) must continue from there. V7 keeps both in sbrk.s. */
extern char end[];              /* user.ld: end of the program */
static char *curbrk = end;

int brk(char *addr)
{
    if (syscall3(SYS_brk, (int)addr, 0, 0) < 0)
        return -1;
    curbrk = addr;
    return 0;
}

/* Grow (or shrink) the heap by incr bytes; returns the old break. */
char *sbrk(int incr)
{
    char *old = curbrk;
    if (brk(curbrk + incr) < 0)
        return (char *)-1;
    return old;
}

/* getuid returns the real uid in EAX and the effective one in EDX. */
int getuid(void)
{
    int euid;
    return syscall3_edx(SYS_getuid, 0, &euid);
}

int geteuid(void)
{
    int euid;
    syscall3_edx(SYS_getuid, 0, &euid);
    return euid;
}

int getgid(void)
{
    int egid;
    return syscall3_edx(SYS_getgid, 0, &egid);
}

int getegid(void)
{
    int egid;
    syscall3_edx(SYS_getgid, 0, &egid);
    return egid;
}

int setuid(int uid)
{
    return syscall3(SYS_setuid, uid, 0, 0);
}

int setgid(int gid)
{
    return syscall3(SYS_setgid, gid, 0, 0);
}

int access(const char *path, int mode)
{
    return syscall3(SYS_access, (int)path, mode, 0);
}

int umask(int mask)
{
    return syscall3(SYS_umask, mask, 0, 0);
}

int utime(const char *path, time_t times[2])
{
    return syscall3(SYS_utime, (int)path, (int)times, 0);
}

/* stime(&t): set the clock (root only). */
int stime(time_t *tp)
{
    return syscall3(SYS_stime, *tp, 0, 0);
}

/* pipe(fd): fd[0] = read end (EAX), fd[1] = write end (EDX). */
int pipe(int *fd)
{
    int w;
    int r = syscall3_edx(SYS_pipe, 0, &w);
    if (r < 0)
        return -1;
    fd[0] = r;
    fd[1] = w;
    return 0;
}

/* ---- signals ---- */

/* The kernel calls a single user address for every caught signal:
 * _sigtramp (sigtramp.asm), with the signal number. _sigtramp then calls
 * the C function remembered here. */
int (*_sigfunc[NSIG])();
void _sigtramp(void);

/* signal(sig, func): func is SIG_DFL, SIG_IGN, or a function to call.
 * Returns the previous one, or BADSIG. */
int (*signal(int sig, int (*func)()))()
{
    int handler = (func == SIG_DFL || func == SIG_IGN) ? (int)func : (int)_sigtramp;
    int old = syscall3(SYS_signal, sig, handler, 0);
    if (old == -1)
        return BADSIG;
    int (*prev)() = old == (int)_sigtramp ? _sigfunc[sig] : (int (*)())old;
    _sigfunc[sig] = func;
    return prev;
}

int kill(int pid, int sig)
{
    return syscall3(SYS_kill, pid, sig, 0);
}

unsigned alarm(unsigned seconds)
{
    return syscall3(SYS_alarm, seconds, 0, 0);
}

int pause(void)
{
    return syscall3(SYS_pause, 0, 0, 0);
}
