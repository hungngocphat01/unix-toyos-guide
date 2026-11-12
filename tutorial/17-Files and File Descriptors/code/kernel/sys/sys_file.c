/* sys_file.c -- file-related system calls. */
#include <stddef.h>
#include "sys/syscall.h"
#include "proc/proc.h"
#include "fs/file.h"
#include "abi/errno.h"
#include "abi/stat.h"

#define PATH_MAX 256

/* fd -> struct file of the current process, or NULL. */
static struct file *fd_file(int fd)
{
    if (fd < 0 || fd >= NOFILE)
        return NULL;
    return current->ofile[fd];
}

/* Put f in the lowest free fd. Returns the fd, or -EMFILE. */
static int fd_alloc(struct file *f)
{
    for (int fd = 0; fd < NOFILE; fd++) {
        if (!current->ofile[fd]) {
            current->ofile[fd] = f;
            return fd;
        }
    }
    return -EMFILE;
}

/* Copy a path from user space and look it up. */
static int user_namei(const char *upath, struct inode **ip)
{
    char path[PATH_MAX];
    int err = fetch_string(upath, path, sizeof(path));
    if (err < 0)
        return err;
    *ip = namei(path);
    return *ip ? 0 : -ENOENT;
}

/* open(path, how): how = 0 read, 1 write, 2 read+write (V7) */
int32_t sys_open(struct trapframe *tf)
{
    const char *upath = (const char *)tf->ebx;
    int how = tf->ecx;
    struct inode *ip;

    if (how < 0 || how > 2)
        return -EINVAL;
    int err = user_namei(upath, &ip);
    if (err < 0)
        return err;

    int mode = how == 0 ? FREAD : how == 1 ? FWRITE : FREAD | FWRITE;
    if (IS_DIR(ip) && (mode & FWRITE)) {
        iput(ip);
        return -EISDIR;
    }
    struct file *f = file_open(ip, mode);
    if (!f) {
        iput(ip);
        return -ENFILE;
    }
    int fd = fd_alloc(f);
    if (fd < 0)
        file_close(f);
    return fd;
}

/* close(fd) */
int32_t sys_close(struct trapframe *tf)
{
    int fd = tf->ebx;
    struct file *f = fd_file(fd);
    if (!f)
        return -EBADF;
    current->ofile[fd] = NULL;
    file_close(f);
    return 0;
}

/* read(fd, buf, n) */
int32_t sys_read(struct trapframe *tf)
{
    struct file *f = fd_file(tf->ebx);
    void *buf = (void *)tf->ecx;
    uint32_t n = tf->edx;

    if (!f)
        return -EBADF;
    if (!user_ok(buf, n, 1))
        return -EFAULT;
    return file_read(f, buf, n);
}

/* write(fd, buf, n) */
int32_t sys_write(struct trapframe *tf)
{
    struct file *f = fd_file(tf->ebx);
    const void *buf = (const void *)tf->ecx;
    uint32_t n = tf->edx;

    if (!f)
        return -EBADF;
    if (!user_ok(buf, n, 0))
        return -EFAULT;
    return file_write(f, buf, n);
}

/* lseek(fd, offset, whence): whence 0 = from start, 1 = from current,
 * 2 = from end. Returns the new offset. */
int32_t sys_lseek(struct trapframe *tf)
{
    struct file *f = fd_file(tf->ebx);
    int32_t off = tf->ecx;
    int whence = tf->edx;

    if (!f)
        return -EBADF;
    if ((f->ip->d.mode & S_IFMT) == S_IFCHR)
        return -ESPIPE;

    int32_t base = whence == 0 ? 0 : whence == 1 ? (int32_t)f->offset
                 : whence == 2 ? (int32_t)f->ip->d.size : -1;
    if (base < 0 || base + off < 0)
        return -EINVAL;
    f->offset = base + off;
    return f->offset;
}

/* dup(fd): a second fd for the same open file (shared offset). */
int32_t sys_dup(struct trapframe *tf)
{
    struct file *f = fd_file(tf->ebx);
    if (!f)
        return -EBADF;
    int fd = fd_alloc(f);
    if (fd >= 0)
        file_dup(f);
    return fd;
}

/* stat(path, &st) */
int32_t sys_stat(struct trapframe *tf)
{
    struct stat *ust = (struct stat *)tf->ecx;
    struct inode *ip;

    if (!user_ok(ust, sizeof(*ust), 1))
        return -EFAULT;
    int err = user_namei((const char *)tf->ebx, &ip);
    if (err < 0)
        return err;
    stat_inode(ip, ust);
    iput(ip);
    return 0;
}

/* fstat(fd, &st) */
int32_t sys_fstat(struct trapframe *tf)
{
    struct file *f = fd_file(tf->ebx);
    struct stat *ust = (struct stat *)tf->ecx;

    if (!f)
        return -EBADF;
    if (!user_ok(ust, sizeof(*ust), 1))
        return -EFAULT;
    stat_inode(f->ip, ust);
    return 0;
}

/* chdir(path) */
int32_t sys_chdir(struct trapframe *tf)
{
    struct inode *ip;
    int err = user_namei((const char *)tf->ebx, &ip);
    if (err < 0)
        return err;
    if (!IS_DIR(ip)) {
        iput(ip);
        return -ENOTDIR;
    }
    iput(current->cwd);
    current->cwd = ip;
    return 0;
}
