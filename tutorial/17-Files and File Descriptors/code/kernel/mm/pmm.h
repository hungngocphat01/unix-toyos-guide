/* pmm.h -- physical memory manager: hands out 4 KiB frames of RAM. */
#ifndef MM_PMM_H
#define MM_PMM_H

#include <stdint.h>
#include "arch/i386/multiboot.h"

void     pmm_init(struct multiboot_info *mbi);
uint32_t pmm_alloc(void);           /* physical address of a zeroed frame, 0 if none */
void     pmm_free(uint32_t pa);
uint32_t pmm_free_count(void);      /* number of free frames */

#endif
