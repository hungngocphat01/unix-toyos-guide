/* io.h -- x86 port I/O. */
#ifndef ARCH_IO_H
#define ARCH_IO_H

#include <stdint.h>

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void outw(uint16_t port, uint16_t val)
{
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port)
{
    uint16_t val;
    __asm__ volatile ("inw %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

/* Read/write count 16-bit words from/to a port into/from memory. */
static inline void insw(uint16_t port, void *buf, uint32_t count)
{
    __asm__ volatile ("rep insw" : "+D"(buf), "+c"(count) : "d"(port) : "memory");
}

static inline void outsw(uint16_t port, const void *buf, uint32_t count)
{
    __asm__ volatile ("rep outsw" : "+S"(buf), "+c"(count) : "d"(port) : "memory");
}

/* A write to unused port 0x80 takes ~1us: gives old chips time to settle. */
static inline void io_wait(void)
{
    outb(0x80, 0);
}

#endif
