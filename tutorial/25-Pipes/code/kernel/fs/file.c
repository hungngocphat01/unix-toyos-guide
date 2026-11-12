/* file.c -- the system-wide open file table. */
#include "fs/file.h"
#include "dev/dev.h"
#include "abi/errno.h"
#include "lib/string.h"

static struct file files[NFILE];

static struct file *file_alloc(int mode)
{
    for (int i = 0; i < NFILE; i++) {
        struct file *f = &files[i];
        if (f->refcnt == 0) {
            memset(f, 0, sizeof(*f));
            f->refcnt = 1;
            f->mode = mode;
            return f;
        }
    }
    return NULL;
}

struct file *file_open(struct inode *ip, int mode)
{
    struct file *f = file_alloc(mode);
    if (f)
        f->ip = ip;
    return f;
}

struct file *file_open_pipe(struct pipe *p, int mode)
{
    struct file *f = file_alloc(mode);
    if (f)
        f->pipe = p;
    return f;
}

struct file *file_dup(struct file *f)
{
    f->refcnt++;
    return f;
}

void file_close(struct file *f)
{
    if (--f->refcnt > 0)
        return;
    if (f->pipe)
        pipe_close(f->pipe, f->mode & FWRITE);
    else
        iput(f->ip);
    f->ip = NULL;
    f->pipe = NULL;
}

int file_read(struct file *f, void *buf, uint32_t n)
{
    struct inode *ip = f->ip;
    int got;

    if (!(f->mode & FREAD))
        return -EBADF;
    if (f->pipe)
        return pipe_read(f->pipe, buf, n);

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
    if (f->pipe)
        return pipe_write(f->pipe, buf, n);

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

    if (f->pipe || (ip->d.mode & S_IFMT) != S_IFCHR)
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
    st->st_size  = IS_DIR(ip) ? dir_size_v7(ip) : ip->d.size;
    st->st_atime = ip->d.atime;
    st->st_mtime = ip->d.mtime;
    st->st_ctime = ip->d.ctime;
}

/* A pipe has no inode. (In V7 it had one, on the root device.) Report a
 * regular file of the size of the data waiting in it, as V7 did, with a
 * device and inode number of its own: programs compare (st_dev, st_ino)
 * to tell files apart. V7 cat refuses to copy a file onto itself, and
 * "cat | cat" must not look like that. */
void stat_file(struct file *f, struct stat *st)
{
    if (!f->pipe) {
        stat_inode(f->ip, st);
        return;
    }
    memset(st, 0, sizeof(*st));
    st->st_dev = PIPEDEV;
    st->st_ino = pipe_id(f->pipe);
    st->st_mode = S_IFREG | 0600;
    st->st_nlink = 0;
    st->st_size = pipe_count(f->pipe);
}
