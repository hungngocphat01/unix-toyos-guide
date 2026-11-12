/* proc.h -- processes. */
#ifndef PROC_PROC_H
#define PROC_PROC_H

#include <stdint.h>
#include "mm/vmm.h"
#include "arch/i386/trap.h"
#include "fs/file.h"

#define NPROC 64                    /* V7: param.h */

enum proc_state {
    UNUSED,                         /* free slot in the table */
    RUNNING,                        /* the one process running now */
    SUSPENDED,                      /* in fork(), waiting for its child to exit */
    ZOMBIE,                         /* exited; parent hasn't collected it */
};

struct proc {
    int              pid;
    enum proc_state  state;
    char             name[16];      /* for debugging */
    struct proc     *parent;

    pde_t           *pd;            /* address space */
    uint32_t         brk_start;     /* end of the loaded program */
    uint32_t         brk;           /* current end of the heap (sbrk) */

    int              exit_status;   /* V7 wait() status, see proc_exit() */

    struct file     *ofile[NOFILE]; /* open files, indexed by fd */
    struct inode    *cwd;           /* current directory */
    int              umask;         /* permission bits creat/mknod turn off */

    /* Who is running this process. The effective ids (uid, gid) decide
     * permissions; the real ids (ruid, rgid) say who started it. They
     * differ while running a setuid program (lesson 22). */
    int              uid, gid;
    int              ruid, rgid;

    /* While SUSPENDED: the user registers to resume with when the child
     * exits (the trap frame of the fork() call, with EAX = child's pid). */
    struct trapframe saved_tf;
};

extern struct proc *current;        /* the running process */

void proc_init(void);

/* Create process 1 running the program `path`, and enter user mode. */
void proc_start_init(const char *path) __attribute__((noreturn));

/* The trap frame of the current process: always at the top of the kernel
 * stack, where the CPU builds it when the process enters the kernel. */
struct trapframe *proc_trapframe(void);

/* Terminate the current process. status is in V7 wait() format:
 *   exit(n)          -> (n & 0xFF) << 8
 *   killed by sig s  -> s                                                */
void proc_exit(int status) __attribute__((noreturn));

/* Create a copy of the current process. Sequential fork: the child runs
 * first, and the parent resumes only when the child exits.
 * Returns -errno, or 0 -- which becomes the CHILD's return value. */
int proc_fork(void);

/* Collect an exited child: returns its pid and stores its status,
 * or -ECHILD if the current process has no children. */
int proc_wait(int *status);

/* Load a new program into the current process (exec.c).
 * argv and envp are NULL-terminated arrays of KERNEL strings.
 * Returns 0 (and the trap frame now starts the new program) or -errno. */
int exec(const char *path, char *const argv[], char *const envp[]);

#endif
