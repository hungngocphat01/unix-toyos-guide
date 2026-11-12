/* kmain.c -- the kernel's C entry point, called from boot.asm. */
#include <stdint.h>
#include "dev/console.h"
#include "lib/kprintf.h"
#include "lib/panic.h"

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002

void kmain(uint32_t magic, uint32_t mbi_phys)
{
    (void)mbi_phys;

    console_init();
    kprintf("toyos booting...\n");

    if (magic != MULTIBOOT_BOOTLOADER_MAGIC)
        panic("not booted by a Multiboot loader (magic=%lx)", magic);

    kprintf("kprintf test: %d %d %x %X \"%s\" '%c' 100%%\n",
            42, -17, 0xdeadbeef, 0xc0ffee, "str", 'x');
    kprintf("padding: [%08x] [%-5d] [%5s] [%d]\n", 0xbeef, 42, "ab", -2147483647 - 1);

    panic("nothing more to do (test %d)", 1);
}
