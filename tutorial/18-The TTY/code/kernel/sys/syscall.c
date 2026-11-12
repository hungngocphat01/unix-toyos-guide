/* syscall.c -- the system call table and dispatcher. */
#include "sys/syscall.h"
#include "abi/syscall.h"
#include "abi/errno.h"
#include "arch/i386/cpu.h"
#include "mm/memlayout.h"
#include "mm/vmm.h"
#include "proc/proc.h"

static syscall_fn syscalls[NSYSCALL] = {
    [SYS_exit]   = sys_exit,
    [SYS_fork]   = sys_fork,
    [SYS_read]   = sys_read,
    [SYS_write]  = sys_write,
    [SYS_open]   = sys_open,
    [SYS_close]  = sys_close,
    [SYS_wait]   = sys_wait,
    [SYS_exec]   = sys_exec,
    [SYS_chdir]  = sys_chdir,
    [SYS_brk]    = sys_brk,
    [SYS_stat]   = sys_stat,
    [SYS_lseek]  = sys_lseek,
    [SYS_getpid] = sys_getpid,
    [SYS_fstat]  = sys_fstat,
    [SYS_dup]    = sys_dup,
    [SYS_ioctl]  = sys_ioctl,
    [SYS_exece]  = sys_exece,
};

void syscall(struct trapframe *tf)
{
    uint32_t num = tf->eax;
    int32_t ret;

    if (num < NSYSCALL && syscalls[num])
        ret = syscalls[num](tf);
    else
        ret = -EINVAL;

    /* Inside the kernel, errors are negative numbers. The ABI says:
     * carry flag set and EAX = the (positive) error number. */
    if (ret < 0) {
        tf->eax = -ret;
        tf->eflags |= EFLAGS_CF;
    } else {
        tf->eax = ret;
        tf->eflags &= ~EFLAGS_CF;
    }
}

int user_ok(const void *p, uint32_t len, int write)
{
    return vmm_check_user(current->pd, (uint32_t)p, len, write) == 0;
}

int fetch_string(const char *src, char *dst, uint32_t max)
{
    for (uint32_t i = 0; i < max; i++) {
        /* Check each new page we step into. */
        if ((i == 0 || ((uint32_t)(src + i) % PAGE_SIZE) == 0) &&
            !user_ok(src + i, 1, 0))
            return -EFAULT;
        dst[i] = src[i];
        if (dst[i] == '\0')
            return i;
    }
    return -E2BIG;
}
