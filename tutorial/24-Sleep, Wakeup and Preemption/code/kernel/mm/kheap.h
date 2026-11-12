/* kheap.h -- kmalloc/kfree for small kernel objects. */
#ifndef MM_KHEAP_H
#define MM_KHEAP_H

#include <stddef.h>

/* Returns ZEROED memory, or NULL. Max size: KMALLOC_MAX bytes.
 * For whole pages, use pmm_alloc() directly. */
void *kmalloc(size_t size);
void  kfree(void *p);

#define KMALLOC_MAX (4096 - 8)

#endif
