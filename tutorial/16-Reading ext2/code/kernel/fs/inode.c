/* inode.c -- the in-memory inode table and reading file data.
 *
 * Like the buffer cache, but for inodes: iget() returns THE struct inode
 * for (dev, inum), loading it from disk the first time; iput() releases it.
 * Two opens of the same file share one struct inode.
 */
#include "fs/fs.h"
#include "fs/bio.h"
#include "lib/string.h"
#include "lib/panic.h"

static struct inode inodes[NINODE];

struct inode *iget(dev_t dev, uint32_t inum)
{
    struct inode *empty = NULL;

    for (int i = 0; i < NINODE; i++) {
        struct inode *ip = &inodes[i];
        if (ip->refcnt > 0 && ip->dev == dev && ip->inum == inum) {
            ip->refcnt++;
            return ip;
        }
        if (ip->refcnt == 0 && !empty)
            empty = ip;
    }
    if (!empty)
        panic("iget: no free inodes");

    /* Not in memory: read it from its inode table block. */
    uint32_t offset;
    struct buf *b = bread(dev, ext2_inode_block(inum, &offset));
    memcpy(&empty->d, b->data + offset, sizeof(empty->d));
    brelse(b);

    empty->dev = dev;
    empty->inum = inum;
    empty->refcnt = 1;
    return empty;
}

struct inode *idup(struct inode *ip)
{
    ip->refcnt++;
    return ip;
}

void iput(struct inode *ip)
{
    assert(ip->refcnt > 0);
    ip->refcnt--;
}

/* Read entry `index` of the block of pointers `blockno`. */
static uint32_t indirect(dev_t dev, uint32_t blockno, uint32_t index)
{
    if (blockno == 0)
        return 0;                           /* hole */
    struct buf *b = bread(dev, blockno);
    uint32_t result = ((uint32_t *)b->data)[index];
    brelse(b);
    return result;
}

/* Which disk block holds block `fb` of the file? (V7's name: bmap.)
 *
 *   fb 0..11                  i_block[fb]                       direct
 *   next 256 blocks           i_block[12] -> [x]                single indirect
 *   next 256*256 blocks       i_block[13] -> [x] -> [y]         double indirect
 *   next 256*256*256 blocks   i_block[14] -> [x] -> [y] -> [z]  triple indirect
 *
 * Returns 0 for a "hole" (a block never written: reads as zeros). */
uint32_t bmap(struct inode *ip, uint32_t fb)
{
    const uint32_t per_block = BLOCK_SIZE / 4;      /* 256 pointers per block */

    if (fb < EXT2_NDIR_BLOCKS)
        return ip->d.block[fb];
    fb -= EXT2_NDIR_BLOCKS;

    if (fb < per_block)
        return indirect(ip->dev, ip->d.block[EXT2_IND_BLOCK], fb);
    fb -= per_block;

    if (fb < per_block * per_block) {
        uint32_t b = indirect(ip->dev, ip->d.block[EXT2_DIND_BLOCK], fb / per_block);
        return indirect(ip->dev, b, fb % per_block);
    }
    fb -= per_block * per_block;

    uint32_t b = indirect(ip->dev, ip->d.block[EXT2_TIND_BLOCK], fb / (per_block * per_block));
    b = indirect(ip->dev, b, (fb / per_block) % per_block);
    return indirect(ip->dev, b, fb % per_block);
}

/* Copy n bytes starting at offset off of the file into dst.
 * Returns the number of bytes copied (less than n at end of file). */
int readi(struct inode *ip, void *dst, uint32_t off, uint32_t n)
{
    char *d = dst;

    if (off >= ip->d.size)
        return 0;
    if (n > ip->d.size - off)
        n = ip->d.size - off;

    for (uint32_t done = 0; done < n; ) {
        uint32_t in_block = off % BLOCK_SIZE;
        uint32_t chunk = BLOCK_SIZE - in_block;
        if (chunk > n - done)
            chunk = n - done;

        uint32_t blockno = bmap(ip, off / BLOCK_SIZE);
        if (blockno == 0) {
            memset(d, 0, chunk);
        } else {
            struct buf *b = bread(ip->dev, blockno);
            memcpy(d, b->data + in_block, chunk);
            brelse(b);
        }
        d += chunk;
        off += chunk;
        done += chunk;
    }
    return n;
}
