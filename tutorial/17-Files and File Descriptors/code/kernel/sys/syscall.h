/* syscall.h -- system call dispatch and helpers for the sys_* functions. */
#ifndef SYS_SYSCALL_H
#define SYS_SYSCALL_H

#include <stdint.h>
#include "arch/i386/trap.h"

/* Called by trap() for int 0x80. Reads the number and arguments from tf
 * and writes the result back into tf (see abi/syscall.h for the ABI). */
void syscall(struct trapframe *tf);

/* Every system call has this shape: arguments come from the trap frame
 * (tf->ebx, tf->ecx, ...). Return a value >= 0, or -ERRNO on failure. */
typedef int32_t (*syscall_fn)(struct trapframe *tf);

/* Check that the current process may read (write=0) or write (write=1)
 * len bytes at user address p. */
int user_ok(const void *p, uint32_t len, int write);

/* Copy a NUL-terminated string from user address src into dst (size max).
 * Returns its length, -EFAULT if unreadable, -E2BIG if too long. */
int fetch_string(const char *src, char *dst, uint32_t max);

/* sys_proc.c */
int32_t sys_exit(struct trapframe *tf);
int32_t sys_getpid(struct trapframe *tf);
int32_t sys_fork(struct trapframe *tf);
int32_t sys_wait(struct trapframe *tf);
int32_t sys_brk(struct trapframe *tf);
int32_t sys_exec(struct trapframe *tf);
int32_t sys_exece(struct trapframe *tf);

/* sys_file.c */
int32_t sys_open(struct trapframe *tf);
int32_t sys_close(struct trapframe *tf);
int32_t sys_read(struct trapframe *tf);
int32_t sys_write(struct trapframe *tf);
int32_t sys_lseek(struct trapframe *tf);
int32_t sys_dup(struct trapframe *tf);
int32_t sys_stat(struct trapframe *tf);
int32_t sys_fstat(struct trapframe *tf);
int32_t sys_chdir(struct trapframe *tf);

#endif
