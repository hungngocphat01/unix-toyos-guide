/* inode.c -- the in-memory inode table, and reading and writing file data.
 *
 * Like the buffer cache, but for inodes: iget() returns THE struct inode
 * for (dev, inum), loading it from disk the first time; iput() releases it.
 * Two opens of the same file share one struct inode.
 */
#include "fs/fs.h"
#include "fs/bio.h"
#include "dev/rtc.h"
#include "abi/errno.h"
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

/* Write the in-memory copy of the inode back to its inode table block.
 * Call it after every change to ip->d (we write through, like bio). */
void iupdate(struct inode *ip)
{
    uint32_t offset;
    struct buf *b = bread(ip->dev, ext2_inode_block(ip->inum, &offset));
    memcpy(b->data + offset, &ip->d, sizeof(ip->d));
    bwrite(b);
    brelse(b);
}

/* Release a reference. When the last reference to a file with no links
 * (no names) goes away, the file is deleted: its blocks and its inode are
 * freed. That's why you can unlink a file while a program has it open:
 * the program keeps using it, and the space comes back when it closes. */
void iput(struct inode *ip)
{
    assert(ip->refcnt > 0);
    if (ip->refcnt == 1 && ip->d.links_count == 0) {
        int was_dir = IS_DIR(ip);
        itrunc(ip);
        ip->d.mode = 0;
        ip->d.dtime = time_now();       /* e2fsck: "deleted at" */
        iupdate(ip);
        ifree(ip->dev, ip->inum, was_dir);
    }
    ip->refcnt--;
}

/* A new block for ip's data or its pointer blocks (zeroed); 0 if full. */
static uint32_t alloc_for(struct inode *ip)
{
    uint32_t blockno = balloc(ip->dev);
    if (blockno)
        ip->d.blocks += BLOCK_SIZE / 512;   /* counted in 512-byte sectors */
    return blockno;
}

/* Pointer i of the inode. With alloc, allocate the block if missing. */
static uint32_t direct(struct inode *ip, int i, int alloc)
{
    if (ip->d.block[i] == 0 && alloc)
        ip->d.block[i] = alloc_for(ip);
    return ip->d.block[i];
}

/* Pointer `index` of the pointer block `blockno` (0 = hole). With alloc,
 * allocate the block it points to if missing. */
static uint32_t indirect(struct inode *ip, uint32_t blockno, uint32_t index, int alloc)
{
    if (blockno == 0)
        return 0;
    struct buf *b = bread(ip->dev, blockno);
    uint32_t *ptr = (uint32_t *)b->data;
    if (ptr[index] == 0 && alloc) {
        ptr[index] = alloc_for(ip);
        bwrite(b);
    }
    uint32_t result = ptr[index];
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
 * Returns 0 for a "hole" (a block never written: reads as zeros).
 * With alloc != 0, missing blocks (data and pointer blocks) are allocated
 * instead; then 0 means the disk is full. The caller must iupdate(ip). */
uint32_t bmap(struct inode *ip, uint32_t fb, int alloc)
{
    const uint32_t per_block = BLOCK_SIZE / 4;      /* 256 pointers per block */

    if (fb < EXT2_NDIR_BLOCKS)
        return direct(ip, fb, alloc);
    fb -= EXT2_NDIR_BLOCKS;

    if (fb < per_block)
        return indirect(ip, direct(ip, EXT2_IND_BLOCK, alloc), fb, alloc);
    fb -= per_block;

    if (fb < per_block * per_block) {
        uint32_t b = indirect(ip, direct(ip, EXT2_DIND_BLOCK, alloc), fb / per_block, alloc);
        return indirect(ip, b, fb % per_block, alloc);
    }
    fb -= per_block * per_block;

    uint32_t b = indirect(ip, direct(ip, EXT2_TIND_BLOCK, alloc), fb / (per_block * per_block), alloc);
    b = indirect(ip, b, (fb / per_block) % per_block, alloc);
    return indirect(ip, b, fb % per_block, alloc);
}

/* Free a block and, if it's a pointer block (depth > 0), everything it
 * points to. depth 1 = single indirect, 2 = double, 3 = triple. */
static void free_tree(dev_t dev, uint32_t blockno, int depth)
{
    if (blockno == 0)
        return;
    for (uint32_t i = 0; depth > 0 && i < BLOCK_SIZE / 4; i++) {
        /* Re-read the pointer each time instead of copying the block onto
         * our small kernel stack. It stays in the buffer cache anyway. */
        struct buf *b = bread(dev, blockno);
        uint32_t next = ((uint32_t *)b->data)[i];
        brelse(b);
        free_tree(dev, next, depth - 1);
    }
    bfree(dev, blockno);
}

/* Throw away the file's contents: size 0, no blocks. */
void itrunc(struct inode *ip)
{
    uint16_t type = ip->d.mode & S_IFMT;
    if (type == S_IFCHR || type == S_IFBLK)
        return;                             /* block[] holds a device number */

    for (int i = 0; i < EXT2_NDIR_BLOCKS; i++)
        free_tree(ip->dev, ip->d.block[i], 0);
    free_tree(ip->dev, ip->d.block[EXT2_IND_BLOCK], 1);
    free_tree(ip->dev, ip->d.block[EXT2_DIND_BLOCK], 2);
    free_tree(ip->dev, ip->d.block[EXT2_TIND_BLOCK], 3);
    memset(ip->d.block, 0, sizeof(ip->d.block));
    ip->d.size = 0;
    ip->d.blocks = 0;
    ip->d.mtime = ip->d.ctime = time_now();
    iupdate(ip);
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

        uint32_t blockno = bmap(ip, off / BLOCK_SIZE, 0);
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

/* Copy n bytes from src into the file at offset off, allocating blocks
 * and growing the file as needed. Returns n, or the number of bytes
 * written before the disk filled up (-ENOSPC if none). */
int writei(struct inode *ip, const void *src, uint32_t off, uint32_t n)
{
    const char *s = src;
    uint32_t done = 0;

    while (done < n) {
        uint32_t in_block = off % BLOCK_SIZE;
        uint32_t chunk = BLOCK_SIZE - in_block;
        if (chunk > n - done)
            chunk = n - done;

        uint32_t blockno = bmap(ip, off / BLOCK_SIZE, 1);
        if (blockno == 0)
            break;                          /* disk full */
        struct buf *b = bread(ip->dev, blockno);
        memcpy(b->data + in_block, s, chunk);
        bwrite(b);
        brelse(b);

        s += chunk;
        off += chunk;
        done += chunk;
    }

    if (off > ip->d.size)
        ip->d.size = off;
    ip->d.mtime = ip->d.ctime = time_now();
    iupdate(ip);                            /* size, times, block pointers */
    return done == 0 && n > 0 ? -ENOSPC : (int)done;
}

/* Device files keep their device number in the block pointers, the way
 * Linux writes them: old format in block[0], or new format in block[1]. */
dev_t inode_rdev(struct inode *ip)
{
    uint32_t old = ip->d.block[0], new = ip->d.block[1];
    if (old)
        return MKDEV((old >> 8) & 0xFF, old & 0xFF);
    return MKDEV((new & 0xFFF00) >> 8, (new & 0xFF) | ((new >> 12) & 0xFFF00));
}

/* mknod writes the old format: major in bits 8-15, minor in bits 0-7. */
void inode_set_rdev(struct inode *ip, dev_t dev)
{
    ip->d.block[0] = (MAJOR(dev) << 8) | MINOR(dev);
}
