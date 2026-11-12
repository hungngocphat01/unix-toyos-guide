/* bootmod.h -- files loaded by the boot loader next to the kernel
 * (QEMU: -initrd "a,b,c"). Our only "filesystem" until lesson 16. */
#ifndef PROC_BOOTMOD_H
#define PROC_BOOTMOD_H

#include <stdint.h>
#include "arch/i386/multiboot.h"

/* Must run before pmm_init(): the module list may live in memory the
 * PMM considers free. */
void bootmod_init(struct multiboot_info *mbi);

/* Find a module by name (the last component of its path).
 * Returns 0 and sets *data and *size, or -1 if there is none. */
int bootmod_find(const char *name, const char **data, uint32_t *size);

#endif
