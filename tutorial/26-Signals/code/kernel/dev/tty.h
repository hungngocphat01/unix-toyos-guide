/* tty.h -- the terminal: line editing between the keyboard and read(). */
#ifndef DEV_TTY_H
#define DEV_TTY_H

#include <stdint.h>

void tty_init(void);

/* A character arrived from the keyboard or the serial port.
 * Called from interrupt handlers. */
void tty_input(char c);

/* cdevsw entry points for /dev/console (and future terminals) */
int tty_read(int minor, void *buf, uint32_t n);
int tty_write(int minor, const void *buf, uint32_t n);
int tty_ioctl(int minor, uint32_t cmd, void *uarg);

#endif
