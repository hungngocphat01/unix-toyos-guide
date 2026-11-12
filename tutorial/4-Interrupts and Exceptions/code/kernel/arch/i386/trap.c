/* trap.c -- every interrupt and exception ends up in trap(). */
#include "arch/i386/trap.h"
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

static void dump_trapframe(struct trapframe *tf)
{
    kprintf("trap %lu (%s), err=%lx\n", tf->trapno,
            tf->trapno < 32 ? exception_names[tf->trapno] : "interrupt", tf->err);
    kprintf("  eip=%08lx cs=%04lx eflags=%08lx\n", tf->eip, tf->cs, tf->eflags);
    kprintf("  eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx\n", tf->eax, tf->ebx, tf->ecx, tf->edx);
    kprintf("  esi=%08lx edi=%08lx ebp=%08lx\n", tf->esi, tf->edi, tf->ebp);
    kprintf("  ds=%04lx es=%04lx fs=%04lx gs=%04lx\n",
            tf->ds & 0xFFFF, tf->es & 0xFFFF, tf->fs & 0xFFFF, tf->gs & 0xFFFF);
}

void trap(struct trapframe *tf)
{
    switch (tf->trapno) {
    case T_BREAKPOINT:
        /* INT3 is a "trap": the saved EIP is already past the instruction,
         * so simply returning continues the program. */
        kprintf("breakpoint at eip=%08lx, continuing\n", tf->eip);
        return;

    default:
        dump_trapframe(tf);
        panic("unhandled trap %lu", tf->trapno);
    }
}
