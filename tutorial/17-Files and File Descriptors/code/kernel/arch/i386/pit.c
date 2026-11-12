/* pit.c -- program PIT channel 0 to raise IRQ 0 HZ times per second. */
#include "arch/i386/pit.h"
#include "arch/i386/pic.h"
#include "arch/i386/trap.h"
#include "arch/i386/io.h"

#define PIT_CH0     0x40
#define PIT_CMD     0x43
#define PIT_FREQ    1193182     /* input clock of the chip, in Hz */

/* channel 0, write low byte then high byte, mode 3 (square wave) */
#define CMD_CH0_LOHI_SQUARE 0x36

static volatile uint32_t ticks;  /* changed by the interrupt handler */

static void timer_irq(void)
{
    ticks++;
}

void pit_init(void)
{
    uint16_t divisor = PIT_FREQ / HZ;

    outb(PIT_CMD, CMD_CH0_LOHI_SQUARE);
    outb(PIT_CH0, divisor & 0xFF);
    outb(PIT_CH0, divisor >> 8);

    irq_register(IRQ_TIMER, timer_irq);
    pic_enable(IRQ_TIMER);
}

uint32_t pit_ticks(void)
{
    return ticks;
}
