/* kmain.c -- the kernel's C entry point, called from boot.asm. */
#include <stdint.h>
#include "arch/i386/gdt.h"
#include "arch/i386/idt.h"
#include "arch/i386/pic.h"
#include "arch/i386/pit.h"
#include "arch/i386/multiboot.h"
#include "dev/console.h"
#include "dev/kbd.h"
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
    pic_init();
    pit_init();
    kbd_init();

    __asm__ volatile ("sti");           /* from now on, IRQs can arrive */
    kprintf("interrupts enabled; type something\n");

    /* Idle loop: sleep until the next interrupt, then check the clock. */
    uint32_t last_second = 0;
    for (;;) {
        __asm__ volatile ("hlt");
        uint32_t second = pit_ticks() / HZ;
        if (second != last_second && second % 5 == 0)
            kprintf("[uptime %lus]\n", second);
        last_second = second;
    }
}
