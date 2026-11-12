/* serial.h -- COM1 (16550 UART). */
#ifndef DEV_SERIAL_H
#define DEV_SERIAL_H

void serial_init(void);
void serial_putc(char c);
void serial_enable_input(void);   /* received bytes -> tty_input */

#endif
