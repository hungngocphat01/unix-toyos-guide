/* fpu.c -- the FPU's registers are part of a process's state, like eax.
 *
 * With preemption, a process can be stopped in the middle of a floating-
 * point computation; another process then uses the same FPU. So the
 * switch saves the FPU registers of the old process (fxsave, 512 bytes)
 * and loads those of the new one (fxrstor). The kernel itself never uses
 * the FPU (-mgeneral-regs-only in the Makefile), so it has nothing to save.
 */
#include "arch/i386/fpu.h"
#include "arch/i386/cpu.h"

struct fpu_state fpu_initial;

void fpu_init(void)
{
    write_cr0((read_cr0() & ~CR0_EM) | CR0_MP | CR0_NE);
    write_cr4(read_cr4() | CR4_OSFXSR);
    __asm__ volatile ("fninit");            /* reset: all exceptions masked */
    fpu_save(&fpu_initial);                 /* every new process starts so */
}

void fpu_save(struct fpu_state *s)
{
    __asm__ volatile ("fxsave %0" : "=m"(*s));
}

void fpu_restore(const struct fpu_state *s)
{
    __asm__ volatile ("fxrstor %0" : : "m"(*s));
}
