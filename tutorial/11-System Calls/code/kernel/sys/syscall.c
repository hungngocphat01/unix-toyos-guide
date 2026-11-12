/* syscall.c -- the system call table and dispatcher. */
#include "sys/syscall.h"
#include "abi/syscall.h"
#include "abi/errno.h"
#include "arch/i386/cpu.h"
#include "mm/memlayout.h"
#include "mm/vmm.h"

static syscall_fn syscalls[NSYSCALL] = {
    [SYS_exit]   = sys_exit,
    [SYS_write]  = sys_write,
    [SYS_getpid] = sys_getpid,
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
    pde_t *pd = P2V(read_cr3());        /* lesson 12: the current process's pd */
    return vmm_check_user(pd, (uint32_t)p, len, write) == 0;
}
