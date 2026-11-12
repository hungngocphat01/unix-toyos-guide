/* proc.c -- the process table.
 *
 * Until lesson 23 only ONE process runs at a time, so there is only ONE
 * kernel stack, shared by everybody. Whenever a process enters the kernel,
 * its trap frame sits at the top of that stack.
 *
 * "Sequential fork": fork() runs the child immediately and suspends the
 * parent (saving its trap frame) until the child exits. So the running
 * process is always the youngest, and every other live process is
 * SUSPENDED in fork(), waiting for its child.
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

static uint32_t kstack_top;

void proc_init(void)
{
    kstack_top = (uint32_t)P2V(pmm_alloc()) + PAGE_SIZE;
    tss_set_kernel_stack(kstack_top);
}

struct trapframe *proc_trapframe(void)
{
    return (struct trapframe *)(kstack_top - sizeof(struct trapframe));
}

/* Find a free slot. Returns NULL if the table is full. */
static struct proc *proc_alloc(void)
{
    for (int i = 0; i < NPROC; i++) {
        struct proc *p = &proc_table[i];
        if (p->state == UNUSED) {
            memset(p, 0, sizeof(*p));
            p->pid = next_pid++;
            return p;
        }
    }
    return NULL;
}

void proc_start_init(const char *path)
{
    struct proc *p = proc_alloc();
    init_proc = p;
    p->state = RUNNING;
    p->pd = vmm_new_pd();               /* empty: exec() replaces it */
    current = p;

    char *argv[] = { (char *)path, NULL };
    char *envp[] = { NULL };
    if (exec(path, argv, envp) < 0)
        panic("cannot exec %s", path);

    kprintf("starting %s as pid %d\n", path, p->pid);
    enter_usermode(proc_trapframe());
}

int proc_fork(void)
{
    struct proc *parent = current;
    struct proc *child = proc_alloc();
    if (!child)
        return -EAGAIN;

    child->pd = uvm_clone(parent->pd);
    if (!child->pd) {
        child->state = UNUSED;
        return -ENOMEM;
    }
    child->parent = parent;
    child->brk_start = parent->brk_start;
    child->brk = parent->brk;
    memcpy(child->name, parent->name, sizeof(child->name));

    /* Suspend the parent: when it resumes, fork() returns the child's pid. */
    struct trapframe *tf = proc_trapframe();
    parent->saved_tf = *tf;
    parent->saved_tf.eax = child->pid;
    parent->saved_tf.eflags &= ~EFLAGS_CF;
    parent->state = SUSPENDED;

    /* Switch to the child. The trap frame on the stack is the same (the
     * child is a copy), and the caller returns 0 into it. */
    child->state = RUNNING;
    current = child;
    write_cr3(V2P(child->pd));
    return 0;
}

int proc_wait(int *status)
{
    int have_children = 0;

    for (int i = 0; i < NPROC; i++) {
        struct proc *p = &proc_table[i];
        if (p->state == UNUSED || p->parent != current)
            continue;
        have_children = 1;
        if (p->state == ZOMBIE) {
            int pid = p->pid;
            *status = p->exit_status;
            p->state = UNUSED;          /* the slot is free again */
            return pid;
        }
    }
    if (!have_children)
        return -ECHILD;
    /* With sequential fork, a child always finishes before its parent runs. */
    panic("proc_wait: a child of pid %d is still alive", current->pid);
}

void proc_exit(int status)
{
    struct proc *p = current;

    /* Switch to the kernel's page directory before freeing ours. */
    write_cr3(V2P(kernel_pd));
    uvm_free(p->pd);
    p->pd = NULL;
    p->exit_status = status;
    p->state = ZOMBIE;

    /* Our children (all zombies by now) become init's children. */
    for (int i = 0; i < NPROC; i++)
        if (proc_table[i].state != UNUSED && proc_table[i].parent == p)
            proc_table[i].parent = init_proc;

    struct proc *parent = p->parent;
    if (!parent) {
        kprintf("init exited with status %x; halting\n", status);
        for (;;)
            hlt();
    }

    /* Resume the parent, which is suspended in fork(): reload its user
     * registers into the trap frame and return to user mode. */
    assert(parent->state == SUSPENDED);
    parent->state = RUNNING;
    current = parent;
    write_cr3(V2P(parent->pd));
    *proc_trapframe() = parent->saved_tf;
    enter_usermode(proc_trapframe());
}
