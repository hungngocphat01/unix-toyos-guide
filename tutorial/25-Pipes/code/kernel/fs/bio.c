/* bio.c -- a cache of disk blocks.
 *
 * Disks are slow, and the filesystem reads the same few blocks (superblock,
 * inode tables, directories) over and over. So every block access goes
 * through a fixed pool of NBUF buffers:
 *
 *   bread(dev, n):  if block n is in a buffer, return that buffer (a "hit");
 *                   otherwise recycle the least recently used free buffer
 *                   and read the block into it (a "miss").
 *   brelse(b):      the caller is done; b moves to the front of the LRU list.
 *
 * Writes go straight to the disk ("write-through"): simple, and nothing is
 * lost if QEMU is killed. V7 delayed writes (bdwrite) for speed.
 */
#include "fs/bio.h"
#include "lib/kprintf.h"
#include "lib/panic.h"

#define NBUF 32

static struct buf bufs[NBUF];
static struct buf lru;              /* list head: lru.next = most recent */
static uint32_t hits, misses;

static void list_remove(struct buf *b)
{
    b->prev->next = b->next;
    b->next->prev = b->prev;
}

static void list_push_front(struct buf *b)
{
    b->next = lru.next;
    b->prev = &lru;
    lru.next->prev = b;
    lru.next = b;
}

void bio_init(void)
{
    lru.next = lru.prev = &lru;
    for (int i = 0; i < NBUF; i++)
        list_push_front(&bufs[i]);
}

/* Find the buffer for (dev, blockno), or recycle one. */
static struct buf *getblk(dev_t dev, uint32_t blockno)
{
    for (struct buf *b = lru.next; b != &lru; b = b->next) {
        if (b->dev == dev && b->blockno == blockno && b->valid) {
            hits++;
            return b;
        }
    }

    /* Not cached: take the least recently used buffer nobody is using. */
    for (struct buf *b = lru.prev; b != &lru; b = b->prev) {
        if (b->refcnt == 0) {
            misses++;
            b->dev = dev;
            b->blockno = blockno;
            b->valid = 0;
            return b;
        }
    }
    panic("bio: all %d buffers are in use", NBUF);
}

struct buf *bread(dev_t dev, uint32_t blockno)
{
    struct buf *b = getblk(dev, blockno);
    b->refcnt++;
    if (!b->valid) {
        if (MAJOR(dev) >= nbdev ||
            bdevsw[MAJOR(dev)].read(MINOR(dev), blockno, b->data) < 0)
            panic("bio: cannot read block %lu of device %x", blockno, dev);
        b->valid = 1;
    }
    return b;
}

void bwrite(struct buf *b)
{
    assert(b->refcnt > 0);
    if (bdevsw[MAJOR(b->dev)].write(MINOR(b->dev), b->blockno, b->data) < 0)
        panic("bio: cannot write block %lu of device %x", b->blockno, b->dev);
    b->valid = 1;
}

void brelse(struct buf *b)
{
    assert(b->refcnt > 0);
    b->refcnt--;
    list_remove(b);
    list_push_front(b);
}

void bio_stats(void)
{
    kprintf("bio: %lu hits, %lu misses\n", hits, misses);
}
