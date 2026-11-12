/* serial.h -- COM1 (16550 UART). */
#ifndef DEV_SERIAL_H
#define DEV_SERIAL_H

void serial_init(void);
void serial_putc(char c);

#endif
