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
#include "dev/dev.h"
#include "fs/bio.h"
#include "mm/memlayout.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "proc/bootmod.h"
#include "proc/proc.h"
#include "lib/kprintf.h"
#include "lib/panic.h"
#include "lib/string.h"

static void bio_test(void)
{
    /* Block 0 = sectors 0 and 1, written by the Makefile and lesson 14. */
    struct buf *b = bread(ROOTDEV, 0);
    kprintf("bio: block 0 starts with \"%s\"\n", (char *)b->data);
    brelse(b);

    struct buf *again = bread(ROOTDEV, 0);       /* cached now: no disk access */
    kprintf("bio: second bread(0) returned the %s buffer\n",
            again == b ? "same" : "a different");
    brelse(again);

    /* Write block 5, push it out of the cache, read it back from disk. */
    b = bread(ROOTDEV, 5);
    memcpy(b->data, "written through the buffer cache", 33);
    bwrite(b);
    brelse(b);
    for (uint32_t i = 100; i < 140; i++)        /* 40 other blocks evict it */
        brelse(bread(ROOTDEV, i));
    b = bread(ROOTDEV, 5);
    kprintf("bio: block 5 read back: \"%s\"\n", (char *)b->data);
    brelse(b);
    bio_stats();
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
    bio_init();
    bio_test();
    proc_init();

    proc_start_init("init");
}
