/* vmm.h -- virtual memory: two-level i386 page tables.
 *
 * A 32-bit virtual address is split 10 / 10 / 12:
 *
 *   31          22 21          12 11           0
 *  +--------------+--------------+--------------+
 *  |  PDX (0-1023)|  PTX (0-1023)| offset (4 KiB)|
 *  +--------------+--------------+--------------+
 *
 * CR3 -> page directory (1024 PDEs) --PDX--> page table (1024 PTEs)
 *     --PTX--> frame; physical address = frame + offset.
 */
#ifndef MM_VMM_H
#define MM_VMM_H

#include <stdint.h>

typedef uint32_t pde_t;     /* page directory entry */
typedef uint32_t pte_t;     /* page table entry     */

/* Flag bits, the same in PDEs and PTEs */
#define PTE_P   0x001       /* present                                */
#define PTE_W   0x002       /* writable                               */
#define PTE_U   0x004       /* user mode may access                   */
#define PTE_PS  0x080       /* (PDE only) 4 MiB page, no page table    */

#define PDX(va)      (((uint32_t)(va) >> 22) & 0x3FF)
#define PTX(va)      (((uint32_t)(va) >> 12) & 0x3FF)
#define PTE_ADDR(e)  ((e) & ~0xFFF)     /* frame address stored in an entry */
#define PTE_FLAGS(e) ((e) & 0xFFF)

extern pde_t *kernel_pd;    /* the kernel's page directory (virtual address) */

void vmm_init(void);

/* Map one page va -> pa with flags (PTE_W, PTE_U). Allocates a page table if
 * needed. Returns 0 on success, -1 if out of memory. */
int  vmm_map(pde_t *pd, uint32_t va, uint32_t pa, uint32_t flags);
void vmm_unmap(pde_t *pd, uint32_t va);

/* Look up va. Returns 0 and sets *pa on success, -1 if not mapped. */
int  vmm_translate(pde_t *pd, uint32_t va, uint32_t *pa);

/* Debug: print each step of the translation of va. */
void vmm_explain(pde_t *pd, uint32_t va);

#endif
