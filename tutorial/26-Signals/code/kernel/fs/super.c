/* super.c -- the superblock, the group descriptors and the bitmaps:
 * mounting, locating inodes, allocating and freeing blocks and inodes.
 * (V7 keeps the equivalent in alloc.c.) */
#include "fs/fs.h"
#include "fs/bio.h"
#include "dev/rtc.h"
#include "mm/kheap.h"
#include "lib/string.h"
#include "lib/kprintf.h"
#include "lib/panic.h"

dev_t rootdev;

static struct ext2_superblock sb;
static struct ext2_group_desc *groups;  /* the group descriptor table */
static uint32_t ngroups;
static uint32_t inode_size;

#define GROUP_TABLE_BLOCK (EXT2_SUPER_BLOCK + 1)

void ext2_mount_root(void)
{
    rootdev = ROOTDEV;

    struct buf *b = bread(rootdev, EXT2_SUPER_BLOCK);
    memcpy(&sb, b->data, sizeof(sb));
    brelse(b);

    if (sb.magic != EXT2_MAGIC)
        panic("ext2: no filesystem on the root disk (magic %x)", sb.magic);
    if ((1024u << sb.log_block_size) != BLOCK_SIZE)
        panic("ext2: block size %u not supported", 1024u << sb.log_block_size);
    if (sb.rev_level > 0 && (sb.feature_incompat & ~EXT2_FEATURE_INCOMPAT_FILETYPE))
        panic("ext2: unsupported features %lx", sb.feature_incompat);

    inode_size = sb.rev_level == 0 ? 128 : sb.inode_size;
    ngroups = (sb.blocks_count - sb.first_data_block + sb.blocks_per_group - 1)
              / sb.blocks_per_group;

    /* The group descriptors start in the block after the superblock. */
    uint32_t table_size = ngroups * sizeof(struct ext2_group_desc);
    groups = kmalloc(table_size);
    if (!groups)
        panic("ext2: too many groups");
    for (uint32_t done = 0; done < table_size; done += BLOCK_SIZE) {
        b = bread(rootdev, GROUP_TABLE_BLOCK + done / BLOCK_SIZE);
        uint32_t n = table_size - done < BLOCK_SIZE ? table_size - done : BLOCK_SIZE;
        memcpy((char *)groups + done, b->data, n);
        brelse(b);
    }

    kprintf("ext2: %lu blocks, %lu inodes, %lu group(s), %lu free blocks\n",
            sb.blocks_count, sb.inodes_count, ngroups, sb.free_blocks_count);
}

/* Inode N is entry (N-1) % inodes_per_group of group (N-1) / inodes_per_group's
 * inode table. Returns the disk block holding it and its offset in that block. */
uint32_t ext2_inode_block(uint32_t inum, uint32_t *offset)
{
    assert(inum >= 1 && inum <= sb.inodes_count);
    uint32_t group = (inum - 1) / sb.inodes_per_group;
    uint32_t index = (inum - 1) % sb.inodes_per_group;
    uint32_t byte = index * inode_size;

    *offset = byte % BLOCK_SIZE;
    return groups[group].inode_table + byte / BLOCK_SIZE;
}

/* ---- writing back the counters ---- */

/* Copy our superblock and group descriptor `g` back to the disk.
 * Only the first sizeof(sb) bytes of the on-disk superblock are ours to
 * overwrite: the rest (fields we don't know about) stays as it is. */
static void write_counts(uint32_t g)
{
    sb.wtime = time_now();

    struct buf *b = bread(rootdev, EXT2_SUPER_BLOCK);
    memcpy(b->data, &sb, sizeof(sb));
    bwrite(b);
    brelse(b);

    uint32_t byte = g * sizeof(struct ext2_group_desc);
    b = bread(rootdev, GROUP_TABLE_BLOCK + byte / BLOCK_SIZE);
    memcpy(b->data + byte % BLOCK_SIZE, &groups[g], sizeof(groups[g]));
    bwrite(b);
    brelse(b);
}

/* Find a 0 bit among the first `nbits` of bitmap block `blockno`, set it,
 * and return its index; -1 if they are all 1. */
static int bitmap_take(uint32_t blockno, uint32_t nbits)
{
    struct buf *b = bread(rootdev, blockno);
    for (uint32_t i = 0; i < nbits; i++) {
        if (!(b->data[i / 8] & (1 << (i % 8)))) {
            b->data[i / 8] |= 1 << (i % 8);
            bwrite(b);
            brelse(b);
            return i;
        }
    }
    brelse(b);
    return -1;
}

static void bitmap_clear(uint32_t blockno, uint32_t i)
{
    struct buf *b = bread(rootdev, blockno);
    if (!(b->data[i / 8] & (1 << (i % 8))))
        panic("ext2: freeing a free block or inode (bitmap %lu, bit %lu)", blockno, i);
    b->data[i / 8] &= ~(1 << (i % 8));
    bwrite(b);
    brelse(b);
}

/* ---- blocks ---- */

/* Allocate a disk block and fill it with zeros. Returns 0 if the disk is full. */
uint32_t balloc(dev_t dev)
{
    assert(dev == rootdev);
    for (uint32_t g = 0; g < ngroups; g++) {
        if (groups[g].free_blocks_count == 0)
            continue;
        int bit = bitmap_take(groups[g].block_bitmap, sb.blocks_per_group);
        if (bit < 0)
            continue;
        groups[g].free_blocks_count--;
        sb.free_blocks_count--;
        write_counts(g);

        uint32_t blockno = sb.first_data_block + g * sb.blocks_per_group + bit;
        struct buf *b = bread(dev, blockno);
        memset(b->data, 0, BLOCK_SIZE);
        bwrite(b);
        brelse(b);
        return blockno;
    }
    return 0;
}

void bfree(dev_t dev, uint32_t blockno)
{
    assert(dev == rootdev);
    uint32_t g = (blockno - sb.first_data_block) / sb.blocks_per_group;
    bitmap_clear(groups[g].block_bitmap, (blockno - sb.first_data_block) % sb.blocks_per_group);
    groups[g].free_blocks_count++;
    sb.free_blocks_count++;
    write_counts(g);
}

/* ---- inodes ---- */

/* Allocate an inode number and initialize the inode on disk: the given
 * mode, no links, no data. Returns it with iget(), or NULL if none is free. */
struct inode *ialloc(dev_t dev, uint16_t mode)
{
    assert(dev == rootdev);
    for (uint32_t g = 0; g < ngroups; g++) {
        if (groups[g].free_inodes_count == 0)
            continue;
        int bit = bitmap_take(groups[g].inode_bitmap, sb.inodes_per_group);
        if (bit < 0)
            continue;
        groups[g].free_inodes_count--;
        sb.free_inodes_count--;
        if ((mode & S_IFMT) == S_IFDIR)
            groups[g].used_dirs_count++;
        write_counts(g);

        struct inode *ip = iget(dev, g * sb.inodes_per_group + bit + 1);
        memset(&ip->d, 0, sizeof(ip->d));
        ip->d.mode = mode;
        ip->d.atime = ip->d.ctime = ip->d.mtime = time_now();
        iupdate(ip);
        return ip;
    }
    return NULL;
}

void ifree(dev_t dev, uint32_t inum, int was_dir)
{
    assert(dev == rootdev);
    uint32_t g = (inum - 1) / sb.inodes_per_group;
    bitmap_clear(groups[g].inode_bitmap, (inum - 1) % sb.inodes_per_group);
    groups[g].free_inodes_count++;
    sb.free_inodes_count++;
    if (was_dir)
        groups[g].used_dirs_count--;
    write_counts(g);
}
