/* kmain.c -- the kernel's C entry point, called from boot.asm. */
#include <stdint.h>
#include "arch/i386/cpu.h"
#include "arch/i386/gdt.h"
#include "arch/i386/idt.h"
#include "arch/i386/pic.h"
#include "arch/i386/pit.h"
#include "arch/i386/multiboot.h"
#include "dev/console.h"
#include "dev/kbd.h"
#include "mm/memlayout.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "lib/kprintf.h"
#include "lib/panic.h"

static void show(const char *what, uint32_t va)
{
    uint32_t pa;
    if (vmm_translate(kernel_pd, va, &pa) == 0)
        kprintf("  %-12s virtual %08lx -> physical %08lx\n", what, va, pa);
    else
        kprintf("  %-12s virtual %08lx -> not mapped\n", what, va);
}

void kmain(uint32_t magic, uint32_t mbi_phys)
{
    console_init();
    kprintf("toyos booting...\n");
    if (magic != MULTIBOOT_BOOTLOADER_MAGIC)
        panic("not booted by a Multiboot loader (magic=%lx)", magic);

    gdt_init();
    idt_init();
    pic_init();
    pit_init();
    kbd_init();
    pmm_init(P2V(mbi_phys));
    vmm_init();

    int local;
    kprintf("higher half:\n");
    show("kmain", (uint32_t)kmain);
    show("stack", (uint32_t)&local);
    show("VGA", (uint32_t)P2V(0xB8000));
    show("old 1 MiB", 0x00100000);

    sti();
    for (;;)
        hlt();
}
