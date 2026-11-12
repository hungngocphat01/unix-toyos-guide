/* syscall.c -- one small C function per system call.
 * The ABI is described in abi/syscall.h. */
#include <errno.h>
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

int exec(const char *path, char **argv)
{
    return syscall3(SYS_exec, (int)path, (int)argv, 0);
}

int exece(const char *path, char **argv, char **envp)
{
    return syscall3(SYS_exece, (int)path, (int)argv, (int)envp);
}

int brk(void *addr)
{
    return syscall3(SYS_brk, (int)addr, 0, 0);
}
