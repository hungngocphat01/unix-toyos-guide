/* console.c -- fans each character out to every output device. */
#include "dev/console.h"
#include "dev/serial.h"
#include "dev/vga.h"

void console_init(void)
{
    serial_init();
    vga_init();
}

void console_putc(char c)
{
    serial_putc(c);
    vga_putc(c);
}
