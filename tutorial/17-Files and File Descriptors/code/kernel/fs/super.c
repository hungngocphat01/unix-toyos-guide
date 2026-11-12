/* super.c -- mount the root filesystem: read and check its superblock and
 * group descriptors, and answer "where is inode N on the disk?". */
#include "fs/fs.h"
#include "fs/bio.h"
#include "mm/kheap.h"
#include "lib/string.h"
#include "lib/kprintf.h"
#include "lib/panic.h"

dev_t rootdev;

static struct ext2_superblock sb;
static struct ext2_group_desc *groups;  /* the group descriptor table */
static uint32_t ngroups;
static uint32_t inode_size;

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

    inode_size = sb.rev_level == 0 ? 128 : sb.inode_size;
    ngroups = (sb.blocks_count - sb.first_data_block + sb.blocks_per_group - 1)
              / sb.blocks_per_group;

    /* The group descriptors start in the block after the superblock. */
    uint32_t table_size = ngroups * sizeof(struct ext2_group_desc);
    groups = kmalloc(table_size);
    if (!groups)
        panic("ext2: too many groups");
    for (uint32_t done = 0; done < table_size; done += BLOCK_SIZE) {
        b = bread(rootdev, EXT2_SUPER_BLOCK + 1 + done / BLOCK_SIZE);
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
