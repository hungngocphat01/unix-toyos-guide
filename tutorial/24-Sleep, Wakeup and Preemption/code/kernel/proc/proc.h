/* proc.h -- processes. */
#ifndef PROC_PROC_H
#define PROC_PROC_H

#include <stdint.h>
#include "mm/vmm.h"
#include "arch/i386/trap.h"
#include "fs/file.h"
#include "arch/i386/fpu.h"

#define NPROC 64                    /* V7: param.h */

enum proc_state {
    UNUSED,                         /* free slot in the table */
    RUNNABLE,                       /* ready to run, waiting for the CPU */
    RUNNING,                        /* on the CPU now */
    SLEEPING,                       /* waiting for an event: see sleep() */
    ZOMBIE,                         /* exited; parent hasn't collected it */
};

/* What swtch() saves of a kernel thread: the registers a C function must
 * preserve, and where it was (eip). Lives on the process's kernel stack. */
struct context {
    uint32_t edi, esi, ebx, ebp;
    uint32_t eip;
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

    /* The process's own kernel stack (one page). When it enters the kernel
     * from user mode, the CPU and isr.asm build its trap frame at the top;
     * when it's not running, its saved context sits below. */
    uint8_t         *kstack;
    struct trapframe *tf;           /* = top of kstack - sizeof(trapframe) */
    struct context  *context;       /* where swtch() saved it */
    void            *chan;          /* while SLEEPING: what it waits for */
    struct fpu_state fpu;           /* FPU registers, while not running */
};

extern struct proc *current;        /* the running process; NULL in the scheduler */

/* Create process 1, ready to run the program `path`. */
void proc_start_init(const char *path);

/* Run processes forever. kmain's last call. */
void scheduler(void) __attribute__((noreturn));

/* Give the CPU to another process; return when it's our turn again. */
void yield(void);

/* Wait for an event. `chan` is any address that names the event (V7
 * calls it a "wait channel"): &tty for "a line was typed", the parent's
 * proc for "a child exited"... Interrupts must be off from the moment
 * the caller checked its condition until here, or the wakeup could come
 * in between and be lost. Always re-check the condition afterwards. */
void sleep(void *chan);

/* Make every process sleeping on `chan` runnable. Safe in interrupt handlers. */
void wakeup(void *chan);

/* ^P on the console: list the processes (for debugging). */
void proc_dump(void);

/* Terminate the current process. status is in V7 wait() format:
 *   exit(n)          -> (n & 0xFF) << 8
 *   killed by sig s  -> s                                                */
void proc_exit(int status) __attribute__((noreturn));

/* Create a copy of the current process. Both run from now on: the
 * parent gets the child's pid (the return value), the child gets 0.
 * Returns -errno on failure. */
int proc_fork(void);

/* Wait for a child to exit, collect it: returns its pid and stores its
 * status, or -ECHILD if the current process has no children. */
int proc_wait(int *status);

/* Load a new program into the current process (exec.c).
 * argv and envp are NULL-terminated arrays of KERNEL strings.
 * Returns 0 (and the trap frame now starts the new program) or -errno. */
int exec(const char *path, char *const argv[], char *const envp[]);

#endif
