/* kmain.c -- the kernel's C entry point, called from boot.asm. */
#include <stdint.h>
#include "arch/i386/gdt.h"
#include "arch/i386/idt.h"
#include "arch/i386/pic.h"
#include "arch/i386/pit.h"
#include "arch/i386/multiboot.h"
#include "dev/console.h"
#include "dev/kbd.h"
#include "dev/ata.h"
#include "mm/memlayout.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "proc/bootmod.h"
#include "proc/proc.h"
#include "lib/kprintf.h"
#include "lib/panic.h"
#include "lib/string.h"

static void disk_test(void)
{
    static char buf[SECTOR_SIZE];

    if (ata_read(0, 1, buf) < 0)
        panic("disk_test: read failed");
    buf[40] = '\0';
    kprintf("disk: sector 0 says \"%s\"\n", buf);

    /* Count boots in sector 1: proof that writes reach the disk image. */
    ata_read(1, 1, buf);
    uint32_t boots = 0;
    if (memcmp(buf, "boots=", 6) == 0)
        boots = *(uint32_t *)(buf + 8);
    boots++;
    memset(buf, 0, SECTOR_SIZE);
    memcpy(buf, "boots=", 6);
    *(uint32_t *)(buf + 8) = boots;
    if (ata_write(1, 1, buf) < 0)
        panic("disk_test: write failed");
    kprintf("disk: this is boot number %lu\n", boots);
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
    bootmod_init(P2V(mbi_phys));
    pmm_init(P2V(mbi_phys));
    vmm_init();
    ata_init();
    disk_test();
    proc_init();

    proc_start_init("init");
}
