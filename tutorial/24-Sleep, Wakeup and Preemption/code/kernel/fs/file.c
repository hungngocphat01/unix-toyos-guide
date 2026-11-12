/* file.c -- the system-wide open file table. */
#include "fs/file.h"
#include "dev/dev.h"
#include "abi/errno.h"
#include "lib/string.h"

static struct file files[NFILE];

struct file *file_open(struct inode *ip, int mode)
{
    for (int i = 0; i < NFILE; i++) {
        struct file *f = &files[i];
        if (f->refcnt == 0) {
            f->refcnt = 1;
            f->mode = mode;
            f->offset = 0;
            f->ip = ip;
            return f;
        }
    }
    return NULL;
}

struct file *file_dup(struct file *f)
{
    f->refcnt++;
    return f;
}

void file_close(struct file *f)
{
    if (--f->refcnt == 0) {
        iput(f->ip);
        f->ip = NULL;
    }
}

int file_read(struct file *f, void *buf, uint32_t n)
{
    struct inode *ip = f->ip;
    int got;

    if (!(f->mode & FREAD))
        return -EBADF;

    switch (ip->d.mode & S_IFMT) {
    case S_IFCHR: {
        dev_t dev = inode_rdev(ip);
        if (MAJOR(dev) >= ncdev)
            return -ENXIO;
        return cdevsw[MAJOR(dev)].read(MINOR(dev), buf, n);
    }
    case S_IFDIR:
        got = dir_read_v7(ip, buf, f->offset, n);
        break;
    default:
        got = readi(ip, buf, f->offset, n);
        break;
    }
    if (got > 0)
        f->offset += got;
    return got;
}

int file_write(struct file *f, const void *buf, uint32_t n)
{
    struct inode *ip = f->ip;

    if (!(f->mode & FWRITE))
        return -EBADF;

    if ((ip->d.mode & S_IFMT) == S_IFCHR) {
        dev_t dev = inode_rdev(ip);
        if (MAJOR(dev) >= ncdev)
            return -ENXIO;
        return cdevsw[MAJOR(dev)].write(MINOR(dev), buf, n);
    }
    int put = writei(ip, buf, f->offset, n);
    if (put > 0)
        f->offset += put;
    return put;
}

int file_ioctl(struct file *f, uint32_t cmd, void *arg)
{
    struct inode *ip = f->ip;

    if ((ip->d.mode & S_IFMT) != S_IFCHR)
        return -ENOTTY;
    dev_t dev = inode_rdev(ip);
    if (MAJOR(dev) >= ncdev || !cdevsw[MAJOR(dev)].ioctl)
        return -ENOTTY;
    return cdevsw[MAJOR(dev)].ioctl(MINOR(dev), cmd, arg);
}

void stat_inode(struct inode *ip, struct stat *st)
{
    memset(st, 0, sizeof(*st));
    st->st_dev   = ip->dev;
    st->st_ino   = ip->inum;
    st->st_mode  = ip->d.mode;
    st->st_nlink = ip->d.links_count;
    st->st_uid   = ip->d.uid;
    st->st_gid   = ip->d.gid;
    st->st_rdev  = inode_rdev(ip);
    st->st_size  = ip->d.size;
    st->st_atime = ip->d.atime;
    st->st_mtime = ip->d.mtime;
    st->st_ctime = ip->d.ctime;
}
