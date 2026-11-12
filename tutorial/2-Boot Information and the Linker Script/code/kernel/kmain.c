/* kmain.c -- the kernel's C entry point, called from boot.asm. */
#include <stdint.h>
#include "arch/i386/multiboot.h"
#include "dev/console.h"
#include "lib/kprintf.h"
#include "lib/panic.h"

/* Defined by linker.ld. Only their ADDRESSES mean something. */
extern char kernel_start[], kernel_end[];

static void print_boot_info(struct multiboot_info *mbi)
{
    kprintf("boot info at %p, memory map at %p\n", mbi, (void *)mbi->mmap_addr);
    if (mbi->flags & MB_INFO_LOADER_NAME)
        kprintf("loader:  %s\n", (char *)mbi->boot_loader_name);
    if (mbi->flags & MB_INFO_CMDLINE)
        kprintf("cmdline: \"%s\"\n", (char *)mbi->cmdline);
    if (mbi->flags & MB_INFO_MEMORY)
        kprintf("memory:  %lu KiB low, %lu KiB high\n", mbi->mem_lower, mbi->mem_upper);

    if (!(mbi->flags & MB_INFO_MEM_MAP))
        panic("loader gave no memory map");

    kprintf("memory map:\n");
    uint32_t addr = mbi->mmap_addr;
    while (addr < mbi->mmap_addr + mbi->mmap_length) {
        struct multiboot_mmap_entry *e = (struct multiboot_mmap_entry *)addr;
        /* Everything fits below 4 GiB on our machine, so print the low 32 bits. */
        kprintf("  %08lx - %08lx  %s\n",
                (uint32_t)e->addr, (uint32_t)(e->addr + e->len - 1),
                e->type == MB_MEMORY_AVAILABLE ? "available" : "reserved");
        addr += e->size + sizeof(e->size);
    }

    kprintf("kernel:  %p - %p (%lu KiB)\n", kernel_start, kernel_end,
            (uint32_t)(kernel_end - kernel_start) / 1024);
}

void kmain(uint32_t magic, uint32_t mbi_phys)
{
    console_init();
    kprintf("toyos booting...\n");

    if (magic != MULTIBOOT_BOOTLOADER_MAGIC)
        panic("not booted by a Multiboot loader (magic=%lx)", magic);

    /* Paging is off: a physical address can be used directly as a pointer. */
    print_boot_info((struct multiboot_info *)mbi_phys);

    panic("nothing more to do");
}
