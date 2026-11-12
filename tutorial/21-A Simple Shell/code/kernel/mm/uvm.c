/* uvm.c -- user virtual memory. */
#include "mm/uvm.h"
#include "mm/pmm.h"
#include "mm/memlayout.h"
#include "lib/string.h"

int uvm_alloc(pde_t *pd, uint32_t start, uint32_t end, uint32_t flags)
{
    uint32_t pa;

    for (uint32_t va = PAGE_ROUND_DOWN(start); va < end; va += PAGE_SIZE) {
        if (vmm_translate(pd, va, &pa) == 0)
            continue;                   /* already mapped (shared with a neighbor) */
        uint32_t frame = pmm_alloc();
        if (!frame || vmm_map(pd, va, frame, flags) < 0) {
            if (frame)
                pmm_free(frame);
            return -1;
        }
    }
    return 0;
}

void uvm_dealloc(pde_t *pd, uint32_t start, uint32_t end)
{
    uint32_t pa;

    for (uint32_t va = PAGE_ROUND_DOWN(start); va < end; va += PAGE_SIZE) {
        if (vmm_translate(pd, va, &pa) == 0) {
            vmm_unmap(pd, va);
            pmm_free(PAGE_ROUND_DOWN(pa));
        }
    }
}

/* The target address space isn't the current one, so we can't just write
 * to va. Instead, find the frame behind each page and write to it through
 * the direct map. */
int uvm_copy_to(pde_t *pd, uint32_t va, const void *src, uint32_t n)
{
    const char *s = src;

    while (n > 0) {
        uint32_t pa;
        if (vmm_translate(pd, va, &pa) < 0)
            return -1;
        uint32_t chunk = PAGE_SIZE - (va % PAGE_SIZE);   /* up to the page end */
        if (chunk > n)
            chunk = n;
        memcpy(P2V(pa), s, chunk);
        s += chunk;
        va += chunk;
        n -= chunk;
    }
    return 0;
}

pde_t *uvm_clone(pde_t *pd)
{
    pde_t *new_pd = vmm_new_pd();
    if (!new_pd)
        return NULL;

    for (uint32_t i = 0; i < PDX(KERNBASE); i++) {
        if (!(pd[i] & PTE_P))
            continue;
        pte_t *pt = P2V(PTE_ADDR(pd[i]));
        for (uint32_t j = 0; j < 1024; j++) {
            if (!(pt[j] & PTE_P))
                continue;
            uint32_t va = (i << 22) | (j << 12);
            uint32_t frame = pmm_alloc();
            if (!frame || vmm_map(new_pd, va, frame, PTE_FLAGS(pt[j])) < 0) {
                if (frame)
                    pmm_free(frame);
                uvm_free(new_pd);
                return NULL;
            }
            memcpy(P2V(frame), P2V(PTE_ADDR(pt[j])), PAGE_SIZE);
        }
    }
    return new_pd;
}

void uvm_free(pde_t *pd)
{
    for (uint32_t i = 0; i < PDX(KERNBASE); i++) {
        if (!(pd[i] & PTE_P))
            continue;
        pte_t *pt = P2V(PTE_ADDR(pd[i]));
        for (int j = 0; j < 1024; j++)
            if (pt[j] & PTE_P)
                pmm_free(PTE_ADDR(pt[j]));
        pmm_free(PTE_ADDR(pd[i]));      /* the page table itself */
    }
    pmm_free(V2P(pd));
}
