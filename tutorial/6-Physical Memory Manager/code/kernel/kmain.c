/* kmain.c -- the kernel's C entry point, called from boot.asm. */
#include <stdint.h>
#include "arch/i386/gdt.h"
#include "arch/i386/idt.h"
#include "arch/i386/pic.h"
#include "arch/i386/pit.h"
#include "arch/i386/multiboot.h"
#include "dev/console.h"
#include "dev/kbd.h"
#include "mm/memlayout.h"
#include "mm/pmm.h"
#include "lib/kprintf.h"
#include "lib/panic.h"

static void pmm_test(void)
{
    uint32_t before = pmm_free_count();
    uint32_t a = pmm_alloc(), b = pmm_alloc(), c = pmm_alloc();
    kprintf("pmm test: allocated %lx %lx %lx\n", a, b, c);

    pmm_free(b);
    uint32_t d = pmm_alloc();
    kprintf("pmm test: freed %lx, next alloc gave %lx\n", b, d);

    pmm_free(a);
    pmm_free(c);
    pmm_free(d);
    assert(pmm_free_count() == before);
    kprintf("pmm test: ok\n");
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
    pmm_test();

    __asm__ volatile ("sti");
    kprintf("idle\n");
    for (;;)
        __asm__ volatile ("hlt");
}
