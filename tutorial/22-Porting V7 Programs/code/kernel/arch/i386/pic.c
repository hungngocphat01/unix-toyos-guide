/* pic.c -- 8259 PIC: routes device IRQ lines 0..15 to CPU vectors.
 *
 * At power-on the master PIC sends IRQ 0..7 as vectors 8..15, which
 * collide with CPU exceptions (IRQ 0, the timer, would look like a double
 * fault!). So we "remap" them to vectors 32..47.
 */
#include <stdint.h>
#include "arch/i386/pic.h"
#include "arch/i386/trap.h"
#include "arch/i386/io.h"

#define MASTER_CMD  0x20
#define MASTER_DATA 0x21
#define SLAVE_CMD   0xA0
#define SLAVE_DATA  0xA1

#define ICW1_INIT_ICW4 0x11     /* start initialization, ICW4 will follow */
#define ICW4_8086      0x01
#define CMD_EOI        0x20     /* "end of interrupt" */

static uint16_t irq_mask = 0xFFFF & ~(1 << IRQ_CASCADE);   /* 1 bit = disabled */

static void write_mask(void)
{
    outb(MASTER_DATA, irq_mask & 0xFF);
    outb(SLAVE_DATA, irq_mask >> 8);
}

void pic_init(void)
{
    /* The four "Initialization Command Words", sent to both chips. */
    outb(MASTER_CMD, ICW1_INIT_ICW4);   io_wait();
    outb(SLAVE_CMD,  ICW1_INIT_ICW4);   io_wait();
    outb(MASTER_DATA, T_IRQ0);          io_wait();  /* ICW2: vector of IRQ 0  */
    outb(SLAVE_DATA,  T_IRQ0 + 8);      io_wait();  /*       vector of IRQ 8  */
    outb(MASTER_DATA, 1 << IRQ_CASCADE); io_wait(); /* ICW3: slave on IRQ 2   */
    outb(SLAVE_DATA,  IRQ_CASCADE);     io_wait();  /*       slave's identity */
    outb(MASTER_DATA, ICW4_8086);       io_wait();
    outb(SLAVE_DATA,  ICW4_8086);       io_wait();

    write_mask();                       /* everything off until enabled */
}

void pic_enable(int irq)
{
    irq_mask &= ~(1 << irq);
    write_mask();
}

/* Tell the PIC we handled the IRQ. Until then it sends no more IRQs of
 * the same or lower priority. IRQ 8..15 go through both chips. */
void pic_eoi(int irq)
{
    if (irq >= 8)
        outb(SLAVE_CMD, CMD_EOI);
    outb(MASTER_CMD, CMD_EOI);
}
