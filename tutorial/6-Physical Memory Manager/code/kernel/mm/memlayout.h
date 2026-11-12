/* memlayout.h -- constants describing physical and virtual memory. */
#ifndef MM_MEMLAYOUT_H
#define MM_MEMLAYOUT_H

#define PAGE_SIZE   4096

/* We manage (and, from lesson 8, map) physical memory below PHYSTOP. */
#define PHYSTOP     0x10000000          /* 256 MiB */

/* Virtual address where physical address 0 appears.
 * Paging is off for now, so virtual == physical: KERNBASE is 0.
 * Lesson 8 changes this ONE number to 0xC0000000. Code that always uses
 * P2V/V2P keeps working unchanged. */
#define KERNBASE    0x00000000

#ifndef __ASSEMBLER__
#include <stdint.h>
#define P2V(pa) ((void *)((uintptr_t)(pa) + KERNBASE))      /* phys -> pointer */
#define V2P(va) ((uintptr_t)(va) - KERNBASE)                /* pointer -> phys */
#endif

#define PAGE_ROUND_UP(a)   (((a) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))
#define PAGE_ROUND_DOWN(a) ((a) & ~(PAGE_SIZE - 1))

#endif
