/* kmain.c -- the kernel's C entry point, called from boot.asm. */
#include <stdint.h>
#include "arch/i386/gdt.h"
#include "arch/i386/idt.h"
#include "arch/i386/pic.h"
#include "arch/i386/pit.h"
#include "arch/i386/multiboot.h"
#include "dev/console.h"
#include "dev/kbd.h"
#include "dev/tty.h"
#include "dev/rtc.h"
#include "dev/serial.h"
#include "dev/ata.h"
#include "dev/dev.h"
#include "fs/bio.h"
#include "fs/fs.h"
#include "mm/memlayout.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "proc/proc.h"
#include "arch/i386/fpu.h"
#include "lib/kprintf.h"
#include "lib/panic.h"

void kmain(uint32_t magic, uint32_t mbi_phys)
{
    console_init();
    kprintf("toyos booting...\n");
    if (magic != MULTIBOOT_BOOTLOADER_MAGIC)
        panic("not booted by a Multiboot loader (magic=%lx)", magic);

    gdt_init();
    fpu_init();
    idt_init();
    pic_init();
    pit_init();
    rtc_init();
    tty_init();
    kbd_init();
    serial_enable_input();
    pmm_init(P2V(mbi_phys));
    vmm_init();
    ata_init();
    bio_init();
    ext2_mount_root();
    proc_start_init("/bin/init");
    scheduler();
}
