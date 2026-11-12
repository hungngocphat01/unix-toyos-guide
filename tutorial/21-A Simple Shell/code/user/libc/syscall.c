/* syscall.c -- one small C function per system call (V7: the .s files in libc/sys).
 * The ABI is described in abi/syscall.h. */
#include <errno.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
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

int brk(char *addr)
{
    return syscall3(SYS_brk, (int)addr, 0, 0);
}
