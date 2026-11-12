/* ext2.h -- the ext2 on-disk format (only the fields we use are named).
 * Reference: https://www.nongnu.org/ext2-doc/ext2.html
 *
 * Disk layout with 1 KiB blocks:
 *
 *   block 0        boot block (unused)
 *   block 1        superblock: sizes and counts for the whole filesystem
 *   block 2..      group descriptor table: one entry per block group
 *   then, for each block group:
 *                  block bitmap, inode bitmap, inode table, data blocks
 */
#ifndef FS_EXT2_H
#define FS_EXT2_H

#include <stdint.h>

#define EXT2_MAGIC          0xEF53
#define EXT2_SUPER_BLOCK    1       /* with 1 KiB blocks */
#define EXT2_ROOT_INO       2       /* inode number of "/" */

struct ext2_superblock {
    uint32_t inodes_count;
    uint32_t blocks_count;
    uint32_t r_blocks_count;
    uint32_t free_blocks_count;
    uint32_t free_inodes_count;
    uint32_t first_data_block;      /* 1 with 1 KiB blocks */
    uint32_t log_block_size;        /* block size = 1024 << this */
    uint32_t log_frag_size;
    uint32_t blocks_per_group;
    uint32_t frags_per_group;
    uint32_t inodes_per_group;
    uint32_t mtime, wtime;
    uint16_t mnt_count, max_mnt_count;
    uint16_t magic;                 /* EXT2_MAGIC */
    uint16_t state, errors, minor_rev_level;
    uint32_t lastcheck, checkinterval, creator_os;
    uint32_t rev_level;             /* 0: inode size 128; 1: see inode_size */
    uint16_t def_resuid, def_resgid;
    uint32_t first_ino;             /* rev 1: first non-reserved inode */
    uint16_t inode_size;            /* rev 1 */
    uint16_t block_group_nr;
    uint32_t feature_compat;
    uint32_t feature_incompat;
    uint32_t feature_ro_compat;
    /* ... more fields we don't use ... */
};

#define EXT2_FEATURE_INCOMPAT_FILETYPE 0x0002   /* the only feature we support */

struct ext2_group_desc {
    uint32_t block_bitmap;          /* block number of the block bitmap */
    uint32_t inode_bitmap;          /* block number of the inode bitmap */
    uint32_t inode_table;           /* first block of the inode table   */
    uint16_t free_blocks_count;
    uint16_t free_inodes_count;
    uint16_t used_dirs_count;
    uint16_t pad;
    uint32_t reserved[3];
};

#define EXT2_NDIR_BLOCKS 12         /* i_block[0..11]: direct blocks       */
#define EXT2_IND_BLOCK   12         /* i_block[12]: single indirect        */
#define EXT2_DIND_BLOCK  13         /* i_block[13]: double indirect        */
#define EXT2_TIND_BLOCK  14         /* i_block[14]: triple indirect        */
#define EXT2_N_BLOCKS    15

struct ext2_inode {
    uint16_t mode;                  /* type and permissions (abi/stat.h)   */
    uint16_t uid;
    uint32_t size;                  /* in bytes                            */
    uint32_t atime, ctime, mtime, dtime;
    uint16_t gid;
    uint16_t links_count;           /* number of directory entries         */
    uint32_t blocks;                /* in 512-byte units (!)               */
    uint32_t flags;
    uint32_t osd1;
    uint32_t block[EXT2_N_BLOCKS];  /* where the data is (see bmap)        */
    uint32_t generation;
    uint32_t file_acl, dir_acl, faddr;
    uint8_t  osd2[12];
};                                  /* 128 bytes */

/* Directory entry. Variable length: rec_len says where the next one is. */
struct ext2_dirent {
    uint32_t inode;                 /* 0 = unused entry                    */
    uint16_t rec_len;               /* bytes from here to the next entry   */
    uint8_t  name_len;
    uint8_t  file_type;             /* "filetype" feature, see below       */
    char     name[];                /* NOT NUL-terminated                  */
};

#define EXT2_FT_UNKNOWN 0
#define EXT2_FT_REG     1
#define EXT2_FT_DIR     2
#define EXT2_FT_CHRDEV  3
#define EXT2_FT_BLKDEV  4

#endif
