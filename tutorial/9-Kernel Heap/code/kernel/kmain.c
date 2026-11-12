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
#include "mm/kheap.h"
#include "lib/kprintf.h"
#include "lib/panic.h"
#include "lib/string.h"

static void kheap_test(void)
{
    char *a = kmalloc(10);          /* 10 + 8 header -> 32-byte block   */
    char *b = kmalloc(100);         /* 108           -> 128-byte block  */
    char *c = kmalloc(1000);        /* 1008          -> 1024-byte block */
    kprintf("kheap test: kmalloc(10)=%p kmalloc(100)=%p kmalloc(1000)=%p\n", a, b, c);

    strncpy(a, "hello", 10);
    kfree(b);
    char *d = kmalloc(120);         /* same size class as b: reuses it */
    kprintf("kheap test: freed %p, kmalloc(120)=%p, a=\"%s\"\n", b, d, a);

    kfree(a);
    kfree(c);
    kfree(d);
    kprintf("kheap test: ok\n");
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

    kheap_test();

    sti();
    for (;;)
        hlt();
}
