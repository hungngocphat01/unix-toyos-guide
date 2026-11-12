/* multiboot.h -- the information a Multiboot (v1) loader hands to the kernel.
 * Spec: https://www.gnu.org/software/grub/manual/multiboot/multiboot.html
 *
 * All addresses inside these structs are PHYSICAL addresses.
 */
#ifndef ARCH_MULTIBOOT_H
#define ARCH_MULTIBOOT_H

#include <stdint.h>

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002

/* Bits in multiboot_info.flags: which fields below are valid */
#define MB_INFO_MEMORY      (1 << 0)    /* mem_lower, mem_upper */
#define MB_INFO_CMDLINE     (1 << 2)    /* cmdline              */
#define MB_INFO_MODS        (1 << 3)    /* mods_count, mods_addr */
#define MB_INFO_MEM_MAP     (1 << 6)    /* mmap_length, mmap_addr */
#define MB_INFO_LOADER_NAME (1 << 9)    /* boot_loader_name     */

struct multiboot_info {
    uint32_t flags;
    uint32_t mem_lower;         /* KiB of memory below 1 MiB          */
    uint32_t mem_upper;         /* KiB of memory above 1 MiB          */
    uint32_t boot_device;
    uint32_t cmdline;           /* C string                           */
    uint32_t mods_count;        /* boot modules (lesson 12)           */
    uint32_t mods_addr;         /* array of struct multiboot_module   */
    uint32_t syms[4];
    uint32_t mmap_length;       /* size in bytes of the memory map    */
    uint32_t mmap_addr;         /* first struct multiboot_mmap_entry  */
    uint32_t drives_length;
    uint32_t drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;  /* C string                           */
};

#define MB_MEMORY_AVAILABLE 1   /* the only type we may use as RAM */

/* Entries have variable size: the next one starts at
 * (char *)entry + entry->size + sizeof(entry->size). */
struct multiboot_mmap_entry {
    uint32_t size;              /* size of the rest of this entry */
    uint64_t addr;
    uint64_t len;
    uint32_t type;
} __attribute__((packed));

struct multiboot_module {
    uint32_t mod_start;         /* first byte                  */
    uint32_t mod_end;           /* one past the last byte      */
    uint32_t cmdline;           /* C string, e.g. "init"       */
    uint32_t reserved;
};

#endif
