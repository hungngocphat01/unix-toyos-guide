/* kmain.c -- first C code that runs.
 *
 * Everything here is deliberately crude: VGA and serial are poked directly.
 * Lesson 2 replaces this with proper drivers (dev/vga.c, dev/serial.c) and
 * a kprintf().
 */
#include <stdint.h>

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002

#define VGA_TEXT   ((volatile uint16_t *)0xB8000)
#define VGA_COLOR  0x0F                 /* white on black */

#define COM1 0x3F8

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

static void serial_putc(char c)
{
    while ((inb(COM1 + 5) & 0x20) == 0)     /* wait for "transmit empty" */
        ;
    outb(COM1, (uint8_t)c);
}

static void say(const char *s)
{
    for (int i = 0; s[i]; i++) {
        if (s[i] == '\n') {
            serial_putc('\r');          /* terminals want CR LF */
        } else {
            VGA_TEXT[i] = (uint16_t)(VGA_COLOR << 8) | (uint8_t)s[i];
        }
        serial_putc(s[i]);
    }
}

void kmain(uint32_t magic, uint32_t mbi_phys)
{
    (void)mbi_phys;

    if (magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        say("Not booted by a Multiboot loader!\n");
        return;
    }

    say("Hello, world from toyos!\n");

    for (;;)
        __asm__ volatile ("hlt");
}
