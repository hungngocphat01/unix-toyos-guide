/* uvm.h -- user virtual memory: managing the part of an address space
 * below KERNBASE. Built on vmm.c (page tables) and pmm.c (frames). */
#ifndef MM_UVM_H
#define MM_UVM_H

#include <stdint.h>
#include "mm/vmm.h"

/* Layout of every user address space */
#define USER_BASE        0x00400000     /* programs are linked here (user/user.ld) */
#define USER_STACK_TOP   0xC0000000     /* = KERNBASE: the stack grows down from here */
#define USER_STACK_PAGES 16             /* 64 KiB, allocated at exec */

/* Map fresh zeroed frames over every unmapped page in [start, end).
 * Returns 0, or -1 if out of memory. */
int  uvm_alloc(pde_t *pd, uint32_t start, uint32_t end, uint32_t flags);

/* Unmap and free every mapped page in [start, end). */
void uvm_dealloc(pde_t *pd, uint32_t start, uint32_t end);

/* Copy n bytes from kernel memory to address va in ANOTHER address space.
 * Returns 0, or -1 if some page isn't mapped. */
int  uvm_copy_to(pde_t *pd, uint32_t va, const void *src, uint32_t n);

/* A new address space with a copy of every user page of pd (for fork).
 * Returns NULL if out of memory. */
pde_t *uvm_clone(pde_t *pd);

/* Free all user memory, the user page tables, and the page directory. */
void uvm_free(pde_t *pd);

#endif
