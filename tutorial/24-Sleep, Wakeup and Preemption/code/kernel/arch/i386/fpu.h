/* fpu.h -- the floating-point unit's registers belong to the process. */
#ifndef ARCH_FPU_H
#define ARCH_FPU_H

#include <stdint.h>

#define FPU_STATE_SIZE 512      /* what fxsave writes */

/* The x87/SSE registers of one process. fxsave needs 16-byte alignment. */
struct fpu_state {
    uint8_t data[FPU_STATE_SIZE];
} __attribute__((aligned(16)));

void fpu_init(void);                        /* enable the FPU, reset it */
void fpu_save(struct fpu_state *s);         /* CPU registers -> s */
void fpu_restore(const struct fpu_state *s);/* s -> CPU registers */
extern struct fpu_state fpu_initial;        /* a freshly reset FPU */

#endif
