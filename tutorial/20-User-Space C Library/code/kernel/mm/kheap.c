/* kheap.c -- a "size class" allocator.
 *
 * Blocks come in power-of-two sizes from 32 to 4096 bytes. Each size has
 * its own free list. When a list is empty, we take a fresh page from the
 * PMM and cut it into blocks of that size.
 *
 * Every block starts with a small header recording its size, so kfree()
 * knows which list to put it back on:
 *
 *     block:  [ header | memory returned to the caller ......... ]
 *                      ^ kmalloc() returns this
 *
 * Pages are never given back to the PMM. Simple, and fine for a kernel
 * whose number of objects stays roughly constant.
 */
#include <stdint.h>
#include "mm/kheap.h"
#include "mm/pmm.h"
#include "mm/memlayout.h"
#include "lib/string.h"
#include "lib/panic.h"

#define MIN_SHIFT 5                 /* smallest block: 32 bytes */
#define MAX_SHIFT 12                /* largest block: 4096 bytes = 1 page */
#define MAGIC     0x6b6d616c        /* "kmal": catches kfree of a bad pointer */

struct header {
    uint32_t magic;
    uint32_t shift;                 /* block size = 1 << shift */
};

struct free_block {
    struct free_block *next;
};

static struct free_block *free_lists[MAX_SHIFT + 1];

/* Cut a new page into blocks of size 1 << shift. */
static int refill(int shift)
{
    uint32_t pa = pmm_alloc();
    if (!pa)
        return -1;

    char *page = P2V(pa);
    for (uint32_t off = 0; off < PAGE_SIZE; off += 1u << shift) {
        struct free_block *b = (struct free_block *)(page + off);
        b->next = free_lists[shift];
        free_lists[shift] = b;
    }
    return 0;
}

void *kmalloc(size_t size)
{
    size_t total = size + sizeof(struct header);
    int shift = MIN_SHIFT;

    while ((1u << shift) < total)
        shift++;
    if (shift > MAX_SHIFT)
        panic("kmalloc: %lu bytes is too big", size);

    if (!free_lists[shift] && refill(shift) < 0)
        return NULL;

    struct free_block *b = free_lists[shift];
    free_lists[shift] = b->next;

    struct header *h = (struct header *)b;
    h->magic = MAGIC;
    h->shift = shift;
    memset(h + 1, 0, (1u << shift) - sizeof(struct header));
    return h + 1;
}

void kfree(void *p)
{
    if (!p)
        return;

    struct header *h = (struct header *)p - 1;
    if (h->magic != MAGIC)
        panic("kfree: %p was not returned by kmalloc (or was freed twice)", p);

    int shift = h->shift;
    h->magic = 0;                   /* so a second kfree is caught */

    struct free_block *b = (struct free_block *)h;
    b->next = free_lists[shift];
    free_lists[shift] = b;
}
