/* idt.h -- Interrupt Descriptor Table. */
#ifndef ARCH_IDT_H
#define ARCH_IDT_H

#include <stdint.h>

void idt_init(void);

/* Who may trigger the vector with an INT instruction: dpl 0 = kernel only,
 * dpl 3 = user programs too (the system call vector, lesson 12). */
void idt_set_gate(int vector, uint32_t handler, int dpl);

#endif
