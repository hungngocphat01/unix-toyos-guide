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
#include "fs/fs.h"
#include "mm/memlayout.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "proc/bootmod.h"
#include "proc/proc.h"
#include "lib/kprintf.h"
#include "lib/panic.h"
#include "lib/string.h"

static void fs_test(void)
{
    /* ls / : read the root directory's raw ext2 entries */
    struct inode *root = namei("/");
    static char block[BLOCK_SIZE];
    kprintf("ls /:");
    for (uint32_t off = 0; off < root->d.size; off += BLOCK_SIZE) {
        readi(root, block, off, BLOCK_SIZE);
        for (uint32_t pos = 0; pos < BLOCK_SIZE; ) {
            struct ext2_dirent *de = (struct ext2_dirent *)(block + pos);
            if (de->inode) {
                char name[NAME_MAX + 1];
                memcpy(name, de->name, de->name_len);
                name[de->name_len] = '\0';
                kprintf(" %s(%lu)", name, de->inode);
            }
            pos += de->rec_len;
        }
    }
    kprintf("\n");
    iput(root);

    /* cat /etc/motd */
    struct inode *motd = namei("/etc/motd");
    if (!motd)
        panic("fs_test: /etc/motd not found");
    char text[64];
    int n = readi(motd, text, 0, sizeof(text) - 1);
    text[n] = '\0';
    kprintf("/etc/motd (%d bytes): %s", n, text);
    iput(motd);

    struct inode *hello = namei("/bin/hello");
    kprintf("/bin/hello: inode %lu, %lu bytes, mode %o\n",
            hello->inum, hello->d.size, hello->d.mode);
    iput(hello);
    kprintf("/nope: %s\n", namei("/nope") ? "found?!" : "not found");
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
    ext2_mount_root();
    fs_test();
    proc_init();

    proc_start_init("init");
}
