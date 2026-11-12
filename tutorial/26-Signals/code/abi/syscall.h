/* abi/syscall.h -- system call numbers. Shared by the kernel and user libc.
 *
 * THE SYSTEM CALL ABI
 *   - A program enters the kernel with  int 0x80.
 *   - EAX = system call number (below; same numbers as V7's sysent.c).
 *   - Arguments, left to right: EBX, ECX, EDX, ESI, EDI.
 *   - On return:
 *       success: carry flag clear, EAX = result
 *       failure: carry flag SET,   EAX = error number (abi/errno.h)
 *     This is V7's convention (the PDP-11 "sys" instruction did the same);
 *     the libc stub turns a set carry into errno = EAX, return -1.
 *   - A few calls return a second value in EDX (V7: r1): getuid and getgid
 *     (the effective id), pipe (the write end).
 *   - All other registers are preserved.
 */
#ifndef ABI_SYSCALL_H
#define ABI_SYSCALL_H

#define SYSCALL_VECTOR 0x80

#define SYS_exit     1
#define SYS_fork     2
#define SYS_read     3
#define SYS_write    4
#define SYS_open     5
#define SYS_close    6
#define SYS_wait     7
#define SYS_creat    8
#define SYS_link     9
#define SYS_unlink  10
#define SYS_exec    11
#define SYS_chdir   12
#define SYS_time    13
#define SYS_mknod   14
#define SYS_chmod   15
#define SYS_chown   16
#define SYS_brk     17      /* V7 calls it "break" */
#define SYS_stat    18
#define SYS_lseek   19
#define SYS_getpid  20
#define SYS_setuid  23
#define SYS_getuid  24
#define SYS_stime   25
#define SYS_alarm   27
#define SYS_fstat   28
#define SYS_pause   29
#define SYS_utime   30
#define SYS_access  33
#define SYS_nice    34
#define SYS_sync    36
#define SYS_kill    37
#define SYS_dup     41      /* dup(fd); dup(fd | DUPFLG, fd2) is dup2 */
#define DUPFLG      0100
#define SYS_pipe    42
#define SYS_times   43
#define SYS_setgid  46
#define SYS_getgid  47
#define SYS_signal  48
#define SYS_ioctl   54
#define SYS_exece   59
#define SYS_umask   60

#define NSYSCALL    64

#endif
