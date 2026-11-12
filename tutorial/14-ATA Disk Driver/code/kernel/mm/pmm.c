/* pmm.c -- a bitmap with one bit per 4 KiB frame below PHYSTOP.
 *
 * bit = 1: frame in use (or not RAM at all), bit = 0: frame is free.
 * We start with everything "used", free what the memory map says is RAM,
 * then mark the kernel and the boot modules used again.
 */
#include "mm/pmm.h"
#include "mm/memlayout.h"
#include "lib/string.h"
#include "lib/kprintf.h"
#include "lib/panic.h"

#define NFRAMES (PHYSTOP / PAGE_SIZE)

static uint32_t bitmap[NFRAMES / 32];
static uint32_t nfree;
static uint32_t next_hint;          /* where to start searching: a speed-up */

extern char kernel_start[], kernel_end[];   /* linker.ld */

static int is_used(uint32_t frame)
{
    return bitmap[frame / 32] & (1u << (frame % 32));
}

static void set_used(uint32_t frame)
{
    if (!is_used(frame))
        nfree--;
    bitmap[frame / 32] |= 1u << (frame % 32);
}

static void set_free(uint32_t frame)
{
    if (is_used(frame))
        nfree++;
    bitmap[frame / 32] &= ~(1u << (frame % 32));
}

/* Free every frame that lies COMPLETELY inside [start, end). */
static void free_range(uint64_t start, uint64_t end)
{
    start = PAGE_ROUND_UP(start);
    end = PAGE_ROUND_DOWN(end);
    if (start < 0x100000)
        start = 0x100000;           /* never use the first 1 MiB */
    if (end > PHYSTOP)
        end = PHYSTOP;
    for (uint64_t a = start; a < end; a += PAGE_SIZE)
        set_free(a / PAGE_SIZE);
}

/* Mark every frame that TOUCHES [start, end) as used. */
static void reserve_range(uint32_t start, uint32_t end)
{
    start = PAGE_ROUND_DOWN(start);
    end = PAGE_ROUND_UP(end);
    for (uint32_t a = start; a < end && a < PHYSTOP; a += PAGE_SIZE)
        set_used(a / PAGE_SIZE);
}

void pmm_init(struct multiboot_info *mbi)
{
    if (!(mbi->flags & MB_INFO_MEM_MAP))
        panic("pmm: no memory map from the boot loader");

    memset(bitmap, 0xFF, sizeof(bitmap));
    nfree = 0;

    uint32_t addr = mbi->mmap_addr;
    while (addr < mbi->mmap_addr + mbi->mmap_length) {
        struct multiboot_mmap_entry *e = P2V(addr);
        if (e->type == MB_MEMORY_AVAILABLE)
            free_range(e->addr, e->addr + e->len);
        addr += e->size + sizeof(e->size);
    }

    /* The kernel image itself. */
    reserve_range(V2P(kernel_start), V2P(kernel_end));

    /* Boot modules (lesson 12): the loader put them somewhere after the kernel. */
    if (mbi->flags & MB_INFO_MODS) {
        struct multiboot_module *mods = P2V(mbi->mods_addr);
        for (uint32_t i = 0; i < mbi->mods_count; i++)
            reserve_range(mods[i].mod_start, mods[i].mod_end);
    }

    kprintf("pmm: %lu frames free (%lu MiB)\n", nfree, nfree / 256);
}

uint32_t pmm_alloc(void)
{
    for (uint32_t i = 0; i < NFRAMES; i++) {
        uint32_t frame = (next_hint + i) % NFRAMES;
        if (!is_used(frame)) {
            set_used(frame);
            next_hint = frame + 1;
            uint32_t pa = frame * PAGE_SIZE;
            memset(P2V(pa), 0, PAGE_SIZE);
            return pa;
        }
    }
    return 0;                       /* out of memory */
}

void pmm_free(uint32_t pa)
{
    uint32_t frame = pa / PAGE_SIZE;

    assert(pa % PAGE_SIZE == 0);
    assert(pa >= 0x100000 && pa < PHYSTOP);
    if (!is_used(frame))
        panic("pmm_free: frame %lx is already free", pa);
    set_free(frame);
}

uint32_t pmm_free_count(void)
{
    return nfree;
}
