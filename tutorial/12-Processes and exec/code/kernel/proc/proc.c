/* proc.c -- the process table.
 *
 * Until lesson 23 only ONE process runs at a time, so there is only ONE
 * kernel stack, shared by everybody. Whenever a process enters the kernel,
 * its trap frame sits at the top of that stack.
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

static struct proc proc_table[NPROC];
static int next_pid = 1;
struct proc *current;

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

void proc_exit(int status)
{
    struct proc *p = current;

    /* Switch to the kernel's page directory before freeing ours. */
    write_cr3(V2P(kernel_pd));
    uvm_free(p->pd);
    p->pd = NULL;
    p->exit_status = status;
    p->state = ZOMBIE;

    kprintf("pid %d (%s) exited, status %x\n", p->pid, p->name, status);

    /* Lesson 13: return to the parent waiting for us. For now pid 1 is
     * the only process, so the system is done. */
    kprintf("no processes left; halting\n");
    for (;;)
        hlt();
}
