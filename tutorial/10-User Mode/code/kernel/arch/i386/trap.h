/* trap.h -- the trap frame: the CPU state saved when an interrupt happens.
 *
 * isr.asm builds this on the stack, then calls trap(tf). Field order is the
 * REVERSE of push order (the stack grows down, a struct grows up):
 *
 *      pushed by the CPU:   [ss, esp only if coming from ring 3], eflags, cs, eip
 *      pushed by the stub:  err (dummy 0 if the CPU pushed none), trapno
 *      pushed by trap_common: ds, es, fs, gs, then PUSHAD
 *                             (eax, ecx, edx, ebx, esp, ebp, esi, edi)
 */
#ifndef ARCH_TRAP_H
#define ARCH_TRAP_H

#include <stdint.h>

struct trapframe {
    /* PUSHAD */
    uint32_t edi, esi, ebp;
    uint32_t esp_unused;        /* ESP before PUSHAD; POPAD ignores it */
    uint32_t ebx, edx, ecx, eax;

    /* segment registers (pushed as 32-bit values) */
    uint32_t gs, fs, es, ds;

    /* pushed by the per-vector stub */
    uint32_t trapno;
    uint32_t err;

    /* pushed by the CPU */
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;

    /* pushed by the CPU only when coming from ring 3 */
    uint32_t user_esp;
    uint32_t user_ss;
};

/* Exception vectors we treat specially */
#define T_DIVIDE     0
#define T_BREAKPOINT 3
#define T_GPFAULT    13
#define T_PAGEFAULT  14

/* Hardware IRQs 0..15 arrive as vectors 32..47 (pic.c sets this up). */
#define T_IRQ0       32
#define NIRQ         16

void trap(struct trapframe *tf);

/* Ask trap() to call handler() whenever IRQ irq fires. */
void irq_register(int irq, void (*handler)(void));

/* isr.asm: load the registers from tf and IRET to it. Never returns. */
void enter_usermode(struct trapframe *tf) __attribute__((noreturn));

#endif
