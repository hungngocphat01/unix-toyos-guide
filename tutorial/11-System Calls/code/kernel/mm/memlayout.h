/* memlayout.h -- constants describing physical and virtual memory. */
#ifndef MM_MEMLAYOUT_H
#define MM_MEMLAYOUT_H

#define PAGE_SIZE   4096

/* We manage and map physical memory below PHYSTOP. */
#define PHYSTOP     0x10000000          /* 256 MiB */

/* Virtual address where physical address 0 appears: all physical memory
 * below PHYSTOP is mapped at KERNBASE + pa (the "direct map").
 * The kernel image (physical 1 MiB) is therefore at 0xC0100000.
 * Must match boot.asm and linker.ld. */
#define KERNBASE    0xC0000000

#ifndef __ASSEMBLER__
#include <stdint.h>
#define P2V(pa) ((void *)((uintptr_t)(pa) + KERNBASE))      /* phys -> pointer */
#define V2P(va) ((uintptr_t)(va) - KERNBASE)                /* pointer -> phys */
#endif

#define PAGE_ROUND_UP(a)   (((a) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))
#define PAGE_ROUND_DOWN(a) ((a) & ~(PAGE_SIZE - 1))

#endif
