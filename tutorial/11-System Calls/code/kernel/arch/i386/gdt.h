/* gdt.h -- segment selectors and GDT setup. */
#ifndef ARCH_GDT_H
#define ARCH_GDT_H

/* A selector is (index << 3) | RPL. RPL = privilege level we claim (0 or 3). */
#define SEG_KCODE  0x08         /* GDT[1], ring 0 */
#define SEG_KDATA  0x10         /* GDT[2], ring 0 */
#define SEG_UCODE  (0x18 | 3)   /* GDT[3], ring 3 */
#define SEG_UDATA  (0x20 | 3)   /* GDT[4], ring 3 */
#define SEG_TSS    0x28         /* GDT[5]         */

#ifndef __ASSEMBLER__
#include <stdint.h>
void gdt_init(void);

/* The kernel stack the CPU switches to when an interrupt arrives in ring 3. */
void tss_set_kernel_stack(uint32_t esp0);
#endif

#endif
