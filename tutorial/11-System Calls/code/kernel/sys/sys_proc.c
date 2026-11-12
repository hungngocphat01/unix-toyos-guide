/* sys_proc.c -- process-related system calls. */
#include "sys/syscall.h"
#include "arch/i386/cpu.h"
#include "lib/kprintf.h"

/* exit(status) */
int32_t sys_exit(struct trapframe *tf)
{
    int status = tf->ebx;

    /* No processes yet (lesson 12): just stop. */
    kprintf("user program called exit(%d)\n", status);
    for (;;)
        hlt();
}

/* getpid() */
int32_t sys_getpid(struct trapframe *tf)
{
    (void)tf;
    return 1;
}
