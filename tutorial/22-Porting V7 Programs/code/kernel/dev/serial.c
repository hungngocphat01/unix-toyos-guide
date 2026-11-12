/* serial.c -- COM1. QEMU's -serial stdio connects it to your terminal:
 * what the kernel sends appears there, and what you type there arrives
 * here, just like the keyboard. */
#include "dev/serial.h"
#include "dev/tty.h"
#include "arch/i386/io.h"
#include "arch/i386/pic.h"
#include "arch/i386/trap.h"

#define COM1 0x3F8

/* Register offsets from COM1 */
#define DATA        0       /* read: received byte, write: byte to send  */
#define INT_ENABLE  1
#define FIFO_CTRL   2
#define LINE_CTRL   3
#define MODEM_CTRL  4
#define LINE_STATUS 5

#define LSR_RX_READY 0x01   /* a received byte is waiting in DATA     */
#define LSR_TX_EMPTY 0x20   /* transmit holding register is empty */

#define IER_RX       0x01   /* interrupt when a byte is received      */

void serial_init(void)
{
    outb(COM1 + INT_ENABLE, 0x00);      /* no interrupts (yet)             */
    outb(COM1 + LINE_CTRL,  0x80);      /* DLAB=1: next 2 regs = divisor   */
    outb(COM1 + DATA,       3);         /* divisor 3 -> 38400 baud (low)   */
    outb(COM1 + INT_ENABLE, 0);         /*                         (high)  */
    outb(COM1 + LINE_CTRL,  0x03);      /* 8 data bits, no parity, 1 stop  */
    outb(COM1 + FIFO_CTRL,  0xC7);      /* enable and clear FIFOs          */
    outb(COM1 + MODEM_CTRL, 0x0B);      /* DTR, RTS, OUT2 (OUT2 gates IRQs) */
}

void serial_putc(char c)
{
    if (c == '\n')
        serial_putc('\r');              /* terminals want CR LF */
    while ((inb(COM1 + LINE_STATUS) & LSR_TX_EMPTY) == 0)
        ;
    outb(COM1 + DATA, (uint8_t)c);
}

static void serial_irq(void)
{
    while (inb(COM1 + LINE_STATUS) & LSR_RX_READY)
        tty_input((char)inb(COM1 + DATA));
}

/* Called once interrupts are set up (console_init runs before the IDT). */
void serial_enable_input(void)
{
    irq_register(IRQ_COM1, serial_irq);
    pic_enable(IRQ_COM1);
    outb(COM1 + INT_ENABLE, IER_RX);
}
