/* console.h -- "the console" = every output device at once (VGA + serial). */
#ifndef DEV_CONSOLE_H
#define DEV_CONSOLE_H

void console_init(void);
void console_putc(char c);

#endif
