/* proc.c -- processes and the scheduler.
 *
 * Every process has its own kernel stack. Whatever a process is doing in
 * the kernel (a system call, waiting for a key...), its state is on that
 * stack. Switching processes = switching kernel stacks (swtch.asm):
 *
 *   process A, in the kernel          scheduler()             process B
 *   ... yield()                       (boot stack)
 *         swtch(&A->context, sched) ──►  picks B
 *                                        swtch(&sched, B->context) ──► ... returns from
 *                                                                      B's own earlier
 *                                                                      swtch()
 *
 * Switching is cooperative for now: a process gives up the CPU only when
 * it waits (wait(), reading the terminal). Lesson 24 adds preemption.
 */
#include "proc/proc.h"
#include "arch/i386/gdt.h"
#include "arch/i386/cpu.h"
#include "mm/memlayout.h"
#include "mm/pmm.h"
#include "mm/uvm.h"
#include "lib/string.h"
#include "lib/kprintf.h"
#include "lib/panic.h"
#include "abi/errno.h"

static struct proc proc_table[NPROC];
static int next_pid = 1;
struct proc *current;
static struct proc *init_proc;

static struct context *scheduler_context;   /* where swtch() returns to the scheduler */

void swtch(struct context **old, struct context *new);     /* swtch.asm */
void trapret(void);                                         /* isr.asm */

/* Find a free slot and give it a kernel stack laid out so that the first
 * swtch() to it "returns" into trapret, which "returns" to user mode with
 * the registers in p->tf:
 *
 *   kstack + PAGE_SIZE  ┌──────────────────┐
 *                       │ struct trapframe │ <- p->tf (filled in by fork or exec)
 *                       ├──────────────────┤
 *                       │ struct context   │ <- p->context, eip = trapret
 *                       └──────────────────┘
 */
static struct proc *proc_alloc(void)
{
    struct proc *p = NULL;
    for (int i = 0; i < NPROC; i++) {
        if (proc_table[i].state == UNUSED) {
            p = &proc_table[i];
            break;
        }
    }
    if (!p)
        return NULL;
    uint32_t page = pmm_alloc();
    if (!page)
        return NULL;

    memset(p, 0, sizeof(*p));
    p->pid = next_pid++;
    p->kstack = P2V(page);

    uint8_t *sp = p->kstack + PAGE_SIZE;
    sp -= sizeof(struct trapframe);
    p->tf = (struct trapframe *)sp;
    sp -= sizeof(struct context);
    p->context = (struct context *)sp;
    p->context->eip = (uint32_t)trapret;
    return p;
}

static void proc_free(struct proc *p)
{
    pmm_free(V2P(p->kstack));
    p->kstack = NULL;
    p->state = UNUSED;
}

void proc_start_init(const char *path)
{
    struct proc *p = proc_alloc();
    init_proc = p;
    p->pd = vmm_new_pd();               /* empty: exec() replaces it */
    p->cwd = namei("/", NULL);          /* uid 0, umask 0: proc_alloc zeroed them */

    current = p;                        /* exec() works on the current process */
    char *argv[] = { (char *)path, NULL };
    char *envp[] = { NULL };
    if (exec(path, argv, envp) < 0)
        panic("cannot exec %s", path);
    current = NULL;
    write_cr3(V2P(kernel_pd));

    kprintf("starting %s as pid %d\n", path, p->pid);
    p->state = RUNNABLE;
}

/* ---- switching ---- */

/* The scheduler runs on the boot stack, between processes. It picks the
 * next RUNNABLE process in the table (round robin) and switches to it.
 * When that process gives up the CPU, swtch() returns here. */
void scheduler(void)
{
    int next = 0;

    for (;;) {
        /* Interrupts are off while we look at the table (an interrupt
         * handler might make a process runnable); open a short window in
         * each round so pending interrupts get handled. */
        __asm__ volatile ("sti; nop; cli");

        struct proc *p = NULL;
        for (int i = 0; i < NPROC && !p; i++) {
            struct proc *q = &proc_table[(next + i) % NPROC];
            if (q->state == RUNNABLE)
                p = q;
        }
        if (!p) {
            __asm__ volatile ("sti; hlt");  /* nothing to do: wait for an interrupt */
            continue;
        }
        next = (p - proc_table + 1) % NPROC;

        /* Run p: its address space, its kernel stack for the next trap. */
        current = p;
        p->state = RUNNING;
        write_cr3(V2P(p->pd));
        tss_set_kernel_stack((uint32_t)p->kstack + PAGE_SIZE);
        swtch(&scheduler_context, p->context);

        /* p gave up the CPU. */
        current = NULL;
        write_cr3(V2P(kernel_pd));
    }
}

/* Go back to the scheduler. The caller has set current->state. */
static void sched(void)
{
    cli();                              /* the scheduler expects interrupts off */
    swtch(&current->context, scheduler_context);
}

void yield(void)
{
    current->state = RUNNABLE;
    sched();
}

/* ---- fork, wait, exit ---- */

int proc_fork(void)
{
    struct proc *parent = current;
    struct proc *child = proc_alloc();
    if (!child)
        return -EAGAIN;

    child->pd = uvm_clone(parent->pd);
    if (!child->pd) {
        proc_free(child);
        return -ENOMEM;
    }
    child->parent = parent;
    child->brk_start = parent->brk_start;
    child->brk = parent->brk;
    for (int fd = 0; fd < NOFILE; fd++)     /* share the parent's open files */
        if (parent->ofile[fd])
            child->ofile[fd] = file_dup(parent->ofile[fd]);
    child->cwd = idup(parent->cwd);
    child->umask = parent->umask;
    child->uid = parent->uid;
    child->gid = parent->gid;
    child->ruid = parent->ruid;
    child->rgid = parent->rgid;
    memcpy(child->name, parent->name, sizeof(child->name));

    /* The child returns to user mode with the parent's registers, except:
     * fork() returns 0, without error. */
    *child->tf = *parent->tf;
    child->tf->eax = 0;
    child->tf->eflags &= ~EFLAGS_CF;

    child->state = RUNNABLE;
    return child->pid;                  /* the parent's return value */
}

int proc_wait(int *status)
{
    for (;;) {
        int have_children = 0;
        for (int i = 0; i < NPROC; i++) {
            struct proc *p = &proc_table[i];
            if (p->state == UNUSED || p->parent != current)
                continue;
            have_children = 1;
            if (p->state == ZOMBIE) {
                int pid = p->pid;
                *status = p->exit_status;
                proc_free(p);           /* its kernel stack is ours to free */
                return pid;
            }
        }
        if (!have_children)
            return -ECHILD;
        yield();                        /* let the children run; look again */
    }
}

void proc_exit(int status)
{
    struct proc *p = current;

    if (p == init_proc) {
        kprintf("init exited with status %x; halting\n", status);
        for (;;)
            hlt();
    }

    for (int fd = 0; fd < NOFILE; fd++) {
        if (p->ofile[fd]) {
            file_close(p->ofile[fd]);
            p->ofile[fd] = NULL;
        }
    }
    iput(p->cwd);
    p->cwd = NULL;

    /* Switch to the kernel's page directory before freeing ours. (We keep
     * running on our kernel stack: the parent frees it in wait().) */
    write_cr3(V2P(kernel_pd));
    uvm_free(p->pd);
    p->pd = kernel_pd;                  /* the scheduler may still load it */
    p->exit_status = status;

    /* Our children become init's children. */
    for (int i = 0; i < NPROC; i++)
        if (proc_table[i].state != UNUSED && proc_table[i].parent == p)
            proc_table[i].parent = init_proc;

    p->state = ZOMBIE;
    sched();
    panic("zombie pid %d ran again", p->pid);
}

/* ---- debugging ---- */

void proc_dump(void)
{
    static const char *names[] = {
        [UNUSED] = "unused", [RUNNABLE] = "runnable",
        [RUNNING] = "running", [ZOMBIE] = "zombie",
    };
    kprintf("\n  PID  PPID  STATE     NAME\n");
    for (int i = 0; i < NPROC; i++) {
        struct proc *p = &proc_table[i];
        if (p->state != UNUSED)
            kprintf("%5d %5d  %-9s %s\n", p->pid, p->parent ? p->parent->pid : 0,
                    names[p->state], p->name);
    }
}
