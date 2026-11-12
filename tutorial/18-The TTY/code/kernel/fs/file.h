/* file.h -- open files (V7: file.h).
 *
 *   process ──fd──► struct file ──► struct inode
 *   ofile[0..19]     offset, mode    the file itself
 *
 * fork() and dup() make two fds share ONE struct file, and therefore one
 * offset. Two separate open()s of the same path get two struct files
 * (two offsets) sharing one inode.
 */
#ifndef FS_FILE_H
#define FS_FILE_H

#include <stdint.h>
#include "fs/fs.h"
#include "abi/stat.h"

#define NFILE  100                  /* open files in the whole system (V7) */
#define NOFILE 20                   /* open files per process (V7)         */

#define FREAD  1
#define FWRITE 2

struct file {
    int           refcnt;
    int           mode;             /* FREAD | FWRITE */
    uint32_t      offset;
    struct inode *ip;
};

/* Open an inode. Takes over the caller's reference to ip.
 * Returns NULL if the file table is full. */
struct file *file_open(struct inode *ip, int mode);
struct file *file_dup(struct file *f);
void         file_close(struct file *f);

/* buf may be a user pointer the caller already checked. Return -errno or
 * the number of bytes transferred. */
int file_read(struct file *f, void *buf, uint32_t n);
int file_write(struct file *f, const void *buf, uint32_t n);
int file_ioctl(struct file *f, uint32_t cmd, void *arg);

/* Fill a V7 struct stat. */
void stat_inode(struct inode *ip, struct stat *st);

#endif
