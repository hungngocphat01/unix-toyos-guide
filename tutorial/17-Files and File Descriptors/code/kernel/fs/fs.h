/* fs.h -- the filesystem layer: in-memory inodes, file data, path names.
 *
 * The names are V7's: iget/iput (inode cache), bmap (file block -> disk
 * block), readi (read file data), namei (path -> inode).
 */
#ifndef FS_FS_H
#define FS_FS_H

#include <stdint.h>
#include "dev/dev.h"
#include "fs/ext2.h"
#include "abi/stat.h"

#define NINODE   64                 /* in-memory inodes (V7: param.h NINODE) */
#define NAME_MAX 255                /* longest name in a directory entry */

/* An inode in memory: the on-disk inode plus bookkeeping. */
struct inode {
    dev_t             dev;
    uint32_t          inum;         /* inode number                      */
    int               refcnt;       /* how many pointers to this struct  */
    struct ext2_inode d;            /* copy of the on-disk inode         */
};

/* super.c */
void ext2_mount_root(void);         /* read the superblock of ROOTDEV */
extern dev_t rootdev;

/* inode.c */
struct inode *iget(dev_t dev, uint32_t inum);   /* get an inode; refcnt++ */
struct inode *idup(struct inode *ip);           /* refcnt++               */
void          iput(struct inode *ip);           /* refcnt--               */
uint32_t      bmap(struct inode *ip, uint32_t file_block);  /* 0 = hole   */
int           readi(struct inode *ip, void *dst, uint32_t off, uint32_t n);
dev_t         inode_rdev(struct inode *ip);     /* device number of a /dev file */

/* dir.c */
struct inode *dirlookup(struct inode *dp, const char *name, int len);
struct inode *namei(const char *path);          /* NULL if not found */

/* Read directory dp as an array of V7 "struct direct" (abi/dir.h).
 * off and n are in bytes of that array. */
int dir_read_v7(struct inode *dp, void *dst, uint32_t off, uint32_t n);

/* Used by inode.c to locate inodes on disk (super.c) */
uint32_t ext2_inode_block(uint32_t inum, uint32_t *offset);

#define IS_DIR(ip) (((ip)->d.mode & S_IFMT) == S_IFDIR)

#endif
