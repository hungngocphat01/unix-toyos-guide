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
uint32_t      balloc(dev_t dev);                /* a zeroed block; 0 = full */
void          bfree(dev_t dev, uint32_t blockno);
struct inode *ialloc(dev_t dev, uint16_t mode); /* NULL = no free inode   */
void          ifree(dev_t dev, uint32_t inum, int was_dir);

/* inode.c */
struct inode *iget(dev_t dev, uint32_t inum);   /* get an inode; refcnt++ */
struct inode *idup(struct inode *ip);           /* refcnt++               */
void          iput(struct inode *ip);           /* refcnt--; maybe delete */
void          iupdate(struct inode *ip);        /* write ip->d to disk    */
uint32_t      bmap(struct inode *ip, uint32_t file_block, int alloc);
void          itrunc(struct inode *ip);         /* size = 0, free blocks  */
int           readi(struct inode *ip, void *dst, uint32_t off, uint32_t n);
int           writei(struct inode *ip, const void *src, uint32_t off, uint32_t n);
dev_t         inode_rdev(struct inode *ip);     /* device number of a /dev file */
void          inode_set_rdev(struct inode *ip, dev_t dev);

/* dir.c */
struct inode *dirlookup(struct inode *dp, const char *name, int len);
/* path -> inode. On failure, NULL and *err (if err isn't NULL) is
 * -ENOENT, -ENOTDIR or -EACCES (no search permission on a directory). */
struct inode *namei(const char *path, int *err);
/* The directory containing the last component of path, which is returned
 * in *name (pointing into path) and *len. */
struct inode *namei_parent(const char *path, const char **name, int *len, int *err);
int  dirlink(struct inode *dp, const char *name, int len, struct inode *ip);
int  dirunlink(struct inode *dp, const char *name, int len);

/* Read directory dp as an array of V7 "struct direct" (abi/dir.h).
 * off and n are in bytes of that array. */
int dir_read_v7(struct inode *dp, void *dst, uint32_t off, uint32_t n);

/* perm.c */
int iaccess(struct inode *ip, int mode);        /* mode: S_IREAD/IWRITE/IEXEC */
int iowner(struct inode *ip);                   /* owner or superuser?        */
int suser(void);                                /* superuser?                 */

/* Used by inode.c to locate inodes on disk (super.c) */
uint32_t ext2_inode_block(uint32_t inum, uint32_t *offset);

#define IS_DIR(ip) (((ip)->d.mode & S_IFMT) == S_IFDIR)

#endif
