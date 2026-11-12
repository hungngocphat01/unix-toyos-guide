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

static volatile uint32_t secret = 0x12345678;

static void paging_demo(void)
{
    /* 1. Walk the translation of a kernel address by hand. */
    vmm_explain(kernel_pd, (uint32_t)&secret);

    /* 2. Map a second virtual page onto the frame that holds `secret`.
     *    Two virtual addresses, one physical byte. */
    uint32_t alias_page = 0x40000000;
    vmm_map(kernel_pd, alias_page, V2P(&secret), PTE_W);
    volatile uint32_t *alias =
        (uint32_t *)(alias_page + ((uint32_t)&secret & 0xFFF));
    *alias = 0xCAFEBABE;
    kprintf("wrote through alias %p; secret at %p is now %lx\n",
            alias, &secret, secret);
    vmm_explain(kernel_pd, (uint32_t)alias);

    /* 3. Touch an address nobody mapped. */
    vmm_unmap(kernel_pd, alias_page);
    kprintf("reading the alias after unmapping it...\n");
    kprintf("%lx\n", *alias);
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

    paging_demo();

    sti();
    for (;;)
        hlt();
}
