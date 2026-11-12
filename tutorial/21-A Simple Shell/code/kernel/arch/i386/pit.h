/* pit.h -- 8253/8254 Programmable Interval Timer: our clock tick. */
#ifndef ARCH_PIT_H
#define ARCH_PIT_H

#include <stdint.h>

#define HZ 100                  /* ticks per second */

void     pit_init(void);
uint32_t pit_ticks(void);       /* ticks since boot */

#endif
