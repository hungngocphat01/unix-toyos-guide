/* pic.h -- the two 8259 Programmable Interrupt Controllers. */
#ifndef ARCH_PIC_H
#define ARCH_PIC_H

/* IRQ n arrives as vector T_IRQ0 + n (see trap.h). */
#define IRQ_TIMER    0
#define IRQ_KEYBOARD 1
#define IRQ_CASCADE  2      /* the slave PIC is wired to the master's IRQ 2 */
#define IRQ_COM1     4
#define IRQ_ATA      14

void pic_init(void);
void pic_enable(int irq);
void pic_eoi(int irq);

#endif
