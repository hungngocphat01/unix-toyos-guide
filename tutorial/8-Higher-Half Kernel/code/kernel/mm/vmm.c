/* vmm.c -- build page tables and turn paging on.
 *
 * Page directories and page tables are just frames from the PMM. The CPU
 * wants PHYSICAL addresses inside them (CR3, PDEs, PTEs); the kernel reads
 * and writes them through P2V(). That's the whole trick.
 */
#include "mm/vmm.h"
#include "mm/pmm.h"
#include "mm/memlayout.h"
#include "arch/i386/cpu.h"
#include "lib/kprintf.h"
#include "lib/panic.h"

pde_t *kernel_pd;

/* Find the PTE that maps va. If its page table doesn't exist: create it
 * when create is set, otherwise return NULL. */
static pte_t *walk(pde_t *pd, uint32_t va, int create)
{
    pde_t *pde = &pd[PDX(va)];

    if (!(*pde & PTE_P)) {
        if (!create)
            return NULL;
        uint32_t pt_pa = pmm_alloc();           /* zeroed: all PTEs not present */
        if (!pt_pa)
            return NULL;
        /* Permissions are checked at BOTH levels and the strictest wins, so
         * the PDE allows everything and each PTE decides. */
        *pde = pt_pa | PTE_P | PTE_W | PTE_U;
    }

    pte_t *pt = P2V(PTE_ADDR(*pde));
    return &pt[PTX(va)];
}

int vmm_map(pde_t *pd, uint32_t va, uint32_t pa, uint32_t flags)
{
    pte_t *pte = walk(pd, va, 1);
    if (!pte)
        return -1;
    if (*pte & PTE_P)
        panic("vmm_map: %lx is already mapped", va);
    *pte = PAGE_ROUND_DOWN(pa) | flags | PTE_P;
    invlpg(va);
    return 0;
}

void vmm_unmap(pde_t *pd, uint32_t va)
{
    pte_t *pte = walk(pd, va, 0);
    if (pte) {
        *pte = 0;
        invlpg(va);     /* the TLB may still remember the old translation */
    }
}

int vmm_translate(pde_t *pd, uint32_t va, uint32_t *pa)
{
    pte_t *pte = walk(pd, va, 0);
    if (!pte || !(*pte & PTE_P))
        return -1;
    *pa = PTE_ADDR(*pte) | (va & 0xFFF);
    return 0;
}

void vmm_explain(pde_t *pd, uint32_t va)
{
    kprintf("translate %08lx: PDX=%lu PTX=%lu offset=%03lx\n",
            va, PDX(va), PTX(va), va & 0xFFF);
    pde_t pde = pd[PDX(va)];
    kprintf("  CR3=%08lx  PDE[%lu]=%08lx", V2P(pd), PDX(va), pde);
    if (!(pde & PTE_P)) {
        kprintf("  (not present)\n");
        return;
    }
    pte_t pte = ((pte_t *)P2V(PTE_ADDR(pde)))[PTX(va)];
    kprintf("  -> page table at %08lx\n  PTE[%lu]=%08lx", PTE_ADDR(pde), PTX(va), pte);
    if (!(pte & PTE_P)) {
        kprintf("  (not present)\n");
        return;
    }
    kprintf("  -> frame %08lx -> physical %08lx\n",
            PTE_ADDR(pte), PTE_ADDR(pte) | (va & 0xFFF));
}

void vmm_init(void)
{
    kernel_pd = P2V(pmm_alloc());

    /* Map all physical memory below PHYSTOP at P2V(pa) = pa + 0xC0000000.
     * No identity map: from now on, a physical address used as a pointer
     * page-faults instead of silently working. */
    for (uint32_t pa = 0; pa < PHYSTOP; pa += PAGE_SIZE)
        if (vmm_map(kernel_pd, (uint32_t)P2V(pa), pa, PTE_W) < 0)
            panic("vmm_init: out of memory");

    /* boot.asm already turned paging on with its temporary boot_pd. */
    write_cr3(V2P(kernel_pd));
    write_cr0(read_cr0() | CR0_WP);
    kprintf("vmm: kernel page directory at %08lx\n", V2P(kernel_pd));
}
