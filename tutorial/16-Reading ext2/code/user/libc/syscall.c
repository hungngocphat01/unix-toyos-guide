/* syscall.c -- one small C function per system call.
 * The ABI is described in abi/syscall.h. */
#include <errno.h>
#include <unistd.h>
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

int write(int fd, const void *buf, unsigned n)
{
    return syscall3(SYS_write, fd, (int)buf, n);
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
