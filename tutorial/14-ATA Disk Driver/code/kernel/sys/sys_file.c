/* sys_file.c -- file-related system calls. */
#include "sys/syscall.h"
#include "abi/errno.h"
#include "dev/console.h"

/* write(fd, buf, n): for now fd 1 and 2 both go to the console. */
int32_t sys_write(struct trapframe *tf)
{
    int fd = tf->ebx;
    const char *buf = (const char *)tf->ecx;
    uint32_t n = tf->edx;

    if (fd != 1 && fd != 2)
        return -EBADF;
    if (!user_ok(buf, n, 0))
        return -EFAULT;

    /* Safe to read buf directly: it's mapped in the current page directory. */
    for (uint32_t i = 0; i < n; i++)
        console_putc(buf[i]);
    return n;
}
