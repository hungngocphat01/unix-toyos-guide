/* serial.c -- COM1 output. QEMU's -serial stdio connects it to your terminal. */
#include "dev/serial.h"
#include "arch/i386/io.h"

#define COM1 0x3F8

/* Register offsets from COM1 */
#define DATA        0       /* read: received byte, write: byte to send  */
#define INT_ENABLE  1
#define FIFO_CTRL   2
#define LINE_CTRL   3
#define MODEM_CTRL  4
#define LINE_STATUS 5

#define LSR_TX_EMPTY 0x20   /* transmit holding register is empty */

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
