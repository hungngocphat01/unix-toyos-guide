/* bio.h -- the buffer cache (V7 names: bread, bwrite, brelse). */
#ifndef FS_BIO_H
#define FS_BIO_H

#include <stdint.h>
#include "dev/dev.h"

struct buf {
    dev_t        dev;
    uint32_t     blockno;
    int          valid;             /* data[] holds the block's contents */
    int          refcnt;            /* users between bread() and brelse() */
    struct buf  *prev, *next;       /* LRU list: most recently used first */
    uint8_t      data[BLOCK_SIZE];
};

void bio_init(void);

/* Return a buffer holding block `blockno` of `dev`, reading it from the
 * disk unless it's already cached. Panics on a disk error. The caller
 * must brelse() it when done. */
struct buf *bread(dev_t dev, uint32_t blockno);

/* Write the buffer's data to the disk now (write-through). */
void bwrite(struct buf *b);

/* Done with the buffer; it stays cached for later bread()s. */
void brelse(struct buf *b);

void bio_stats(void);       /* print hit/miss counters */

#endif
