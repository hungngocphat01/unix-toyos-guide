/* kmain.c -- the kernel's C entry point, called from boot.asm. */
#include <stdint.h>
#include "arch/i386/gdt.h"
#include "arch/i386/idt.h"
#include "arch/i386/multiboot.h"
#include "dev/console.h"
#include "lib/kprintf.h"
#include "lib/panic.h"

void kmain(uint32_t magic, uint32_t mbi_phys)
{
    (void)mbi_phys;

    console_init();
    kprintf("toyos booting...\n");
    if (magic != MULTIBOOT_BOOTLOADER_MAGIC)
        panic("not booted by a Multiboot loader (magic=%lx)", magic);

    gdt_init();
    idt_init();
    kprintf("GDT and IDT loaded\n");

    /* Test 1: an exception we handle and return from. */
    __asm__ volatile ("int3");
    kprintf("back in kmain after int3\n");

    /* Test 2: an exception we can't recover from. Written in assembly
     * because GCC is allowed to compile a C division by zero into anything. */
    kprintf("dividing by zero...\n");
    __asm__ volatile ("mov $0x1234, %%eax; xor %%edx, %%edx; xor %%ecx, %%ecx; div %%ecx"
                      : : : "eax", "ecx", "edx");

    panic("not reached");
}
