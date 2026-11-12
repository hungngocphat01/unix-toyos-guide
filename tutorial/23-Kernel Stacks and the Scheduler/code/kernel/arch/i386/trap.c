/* trap.c -- every interrupt and exception ends up in trap(). */
#include "arch/i386/trap.h"
#include "arch/i386/pic.h"
#include "arch/i386/cpu.h"
#include "sys/syscall.h"
#include "proc/proc.h"
#include "abi/signal.h"
#include "lib/kprintf.h"
#include "lib/panic.h"

static const char *exception_names[32] = {
    "divide error", "debug", "NMI", "breakpoint", "overflow",
    "bound range", "invalid opcode", "device not available",
    "double fault", "coprocessor overrun", "invalid TSS",
    "segment not present", "stack fault", "general protection",
    "page fault", "reserved", "x87 FPU error", "alignment check",
    "machine check", "SIMD error", "virtualization", "control protection",
    "reserved", "reserved", "reserved", "reserved", "reserved", "reserved",
    "hypervisor injection", "VMM communication", "security", "reserved",
};

static void (*irq_handlers[NIRQ])(void);

/* The low 2 bits of the saved CS = the privilege level we were running at. */
static int from_user(struct trapframe *tf)
{
    return (tf->cs & 3) == 3;
}

void irq_register(int irq, void (*handler)(void))
{
    irq_handlers[irq] = handler;
}

/* The signal a user program "receives" for each kind of fault. */
static int fault_signal(uint32_t trapno)
{
    switch (trapno) {
    case T_DIVIDE:      return SIGFPE;
    case T_GPFAULT:
    case T_PAGEFAULT:   return SIGSEGV;
    default:            return SIGILL;
    }
}

static void dump_trapframe(struct trapframe *tf)
{
    kprintf("trap %lu (%s), err=%lx\n", tf->trapno,
            tf->trapno < 32 ? exception_names[tf->trapno] : "interrupt", tf->err);
    if (tf->trapno == T_PAGEFAULT)
        kprintf("  address=%08lx (%s, %s, %s mode)\n", read_cr2(),
                tf->err & 1 ? "protection violation" : "page not present",
                tf->err & 2 ? "write" : "read",
                tf->err & 4 ? "user" : "kernel");
    kprintf("  eip=%08lx cs=%04lx eflags=%08lx\n", tf->eip, tf->cs, tf->eflags);
    kprintf("  eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx\n", tf->eax, tf->ebx, tf->ecx, tf->edx);
    kprintf("  esi=%08lx edi=%08lx ebp=%08lx\n", tf->esi, tf->edi, tf->ebp);
    kprintf("  ds=%04lx es=%04lx fs=%04lx gs=%04lx\n",
            tf->ds & 0xFFFF, tf->es & 0xFFFF, tf->fs & 0xFFFF, tf->gs & 0xFFFF);
    if (from_user(tf))
        kprintf("  user esp=%08lx ss=%04lx\n", tf->user_esp, tf->user_ss & 0xFFFF);
}

void trap(struct trapframe *tf)
{
    if (tf->trapno >= T_IRQ0 && tf->trapno < T_IRQ0 + NIRQ) {
        int irq = tf->trapno - T_IRQ0;
        /* EOI first: a handler might not return quickly (lesson 24). The
         * interrupt gate keeps IF=0, so the PIC can't interrupt us anyway. */
        pic_eoi(irq);
        if (irq_handlers[irq])
            irq_handlers[irq]();
        return;
    }

    switch (tf->trapno) {
    case T_SYSCALL:
        syscall(tf);
        return;

    case T_BREAKPOINT:
        /* INT3 is a "trap": the saved EIP is already past the instruction,
         * so simply returning continues the program. */
        kprintf("breakpoint at eip=%08lx, continuing\n", tf->eip);
        return;

    default:
        dump_trapframe(tf);
        if (from_user(tf)) {
            /* A bug in a user program kills the program, not the kernel. */
            kprintf("pid %d (%s) killed\n", current->pid, current->name);
            proc_exit(fault_signal(tf->trapno));
        }
        panic("unhandled trap %lu", tf->trapno);
    }
}
