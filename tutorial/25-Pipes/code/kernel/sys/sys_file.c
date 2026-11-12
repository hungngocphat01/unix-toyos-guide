/* sys_file.c -- file-related system calls. */
#include <stddef.h>
#include "sys/syscall.h"
#include "proc/proc.h"
#include "fs/file.h"
#include "lib/string.h"
#include "abi/errno.h"
#include "abi/stat.h"
#include "abi/signal.h"
#include "abi/syscall.h"
#include "dev/rtc.h"

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
    *ip = namei(path, &err);
    return *ip ? 0 : err;
}

/* Copy a path from user space and find its parent directory. The last
 * component is copied to name (NUL-terminated). */
static int user_namei_parent(const char *upath, struct inode **dp, char *name)
{
    char path[PATH_MAX];
    const char *last;
    int len;
    int err = fetch_string(upath, path, sizeof(path));
    if (err < 0)
        return err;
    *dp = namei_parent(path, &last, &len, &err);
    if (!*dp)
        return err;
    memcpy(name, last, len);
    name[len] = '\0';
    return 0;
}

/* Make a new file `upath` with this mode (type and permissions), its one
 * link being its entry in the parent directory. Returns 0 and the inode,
 * or -errno (-EEXIST if the name is taken). */
static int create(const char *upath, uint16_t mode, struct inode **result)
{
    struct inode *dp;
    char name[NAME_MAX + 1];
    int err = user_namei_parent(upath, &dp, name);
    if (err < 0)
        return err;

    struct inode *ip = dirlookup(dp, name, strlen(name));
    if (ip) {
        iput(ip);
        iput(dp);
        return -EEXIST;
    }
    if ((err = iaccess(dp, S_IWRITE)) < 0) {   /* adding a name = writing dp */
        iput(dp);
        return err;
    }
    mode &= ~current->umask;                    /* (the type bits are above 07777) */
    ip = ialloc(dp->dev, mode);
    if (!ip) {
        iput(dp);
        return -ENOSPC;
    }
    err = dirlink(dp, name, strlen(name), ip);
    iput(dp);
    if (err < 0) {
        iput(ip);                   /* no links: iput frees it again */
        return err;
    }
    ip->d.links_count = 1;
    ip->d.uid = current->uid;                   /* the new file is ours */
    ip->d.gid = current->gid;
    iupdate(ip);
    *result = ip;
    return 0;
}

/* Wrap an inode in a struct file and an fd. Consumes the inode reference. */
static int open_inode(struct inode *ip, int mode)
{
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
    if (IS_DIR(ip) && (mode & FWRITE))
        err = -EISDIR;
    else if (mode & FREAD)
        err = iaccess(ip, S_IREAD);
    if (err == 0 && (mode & FWRITE))
        err = iaccess(ip, S_IWRITE);
    if (err < 0) {
        iput(ip);
        return err;
    }
    return open_inode(ip, mode);
}

/* creat(path, mode): create a regular file, or empty an existing one.
 * Opens it for writing. (V7 has no O_CREAT: this is how files are made.) */
int32_t sys_creat(struct trapframe *tf)
{
    const char *upath = (const char *)tf->ebx;
    uint16_t perm = tf->ecx & 07777;
    struct inode *ip;

    int err = create(upath, S_IFREG | perm, &ip);
    if (err == -EEXIST) {
        user_namei(upath, &ip);
        err = IS_DIR(ip) ? -EISDIR : iaccess(ip, S_IWRITE);
        if (err < 0) {
            iput(ip);
            return err;
        }
        itrunc(ip);                 /* keep its mode and owner, drop the data */
    } else if (err < 0) {
        return err;
    }
    return open_inode(ip, FWRITE);
}

/* mknod(path, mode, dev): make a file of any type. mode includes the type:
 * 020666 = character device, 040755 = (empty!) directory. V7's mkdir
 * program then links "." and ".." into it. */
int32_t sys_mknod(struct trapframe *tf)
{
    uint16_t mode = tf->ecx;
    struct inode *ip;

    int err = suser();              /* V7: only root makes devices and directories */
    if (err < 0)
        return err;
    if ((mode & S_IFMT) == 0)
        mode |= S_IFREG;
    err = create((const char *)tf->ebx, mode, &ip);
    if (err < 0)
        return err;
    if ((mode & S_IFMT) == S_IFCHR || (mode & S_IFMT) == S_IFBLK) {
        inode_set_rdev(ip, tf->edx);
        iupdate(ip);
    }
    iput(ip);
    return 0;
}

/* link(old, new): a second name for the same file. */
int32_t sys_link(struct trapframe *tf)
{
    struct inode *ip, *dp;
    char name[NAME_MAX + 1];

    int err = user_namei((const char *)tf->ebx, &ip);
    if (err < 0)
        return err;
    err = user_namei_parent((const char *)tf->ecx, &dp, name);
    if (err < 0) {
        iput(ip);
        return err;
    }

    struct inode *old = dirlookup(dp, name, strlen(name));
    if (old) {
        iput(old);
        err = -EEXIST;
    } else if (IS_DIR(ip) && (err = suser()) < 0) {
        ;                           /* only root links directories (mkdir, mv) */
    } else if ((err = iaccess(dp, S_IWRITE)) < 0) {
        ;
    } else if (dp->dev != ip->dev) {
        err = -EXDEV;
    } else {
        err = dirlink(dp, name, strlen(name), ip);
    }
    if (err == 0) {
        ip->d.links_count++;
        ip->d.ctime = time_now();
        iupdate(ip);
    }
    iput(dp);
    iput(ip);
    return err;
}

/* unlink(path): remove a name. The file itself is deleted when it has
 * no names left and nobody has it open (see iput). Like V7, unlinking a
 * directory is allowed (that's how rmdir works): the rmdir program checks
 * it's empty, and unlinks "dir/.", "dir/.." and "dir". */
int32_t sys_unlink(struct trapframe *tf)
{
    struct inode *dp;
    char name[NAME_MAX + 1];

    int err = user_namei_parent((const char *)tf->ebx, &dp, name);
    if (err < 0)
        return err;
    struct inode *ip = dirlookup(dp, name, strlen(name));
    if (!ip) {
        iput(dp);
        return -ENOENT;
    }
    err = iaccess(dp, S_IWRITE);    /* removing a name = writing dp */
    if (err == 0 && IS_DIR(ip))
        err = suser();              /* only root unlinks directories (rmdir) */
    if (err < 0) {
        iput(ip);
        iput(dp);
        return err;
    }

    dirunlink(dp, name, strlen(name));
    ip->d.links_count--;
    ip->d.ctime = time_now();
    iupdate(ip);
    iput(dp);
    iput(ip);                       /* maybe the last reference: delete */
    return 0;
}

/* chmod(path, mode): change the permission bits (not the type). */
int32_t sys_chmod(struct trapframe *tf)
{
    struct inode *ip;
    int err = user_namei((const char *)tf->ebx, &ip);
    if (err < 0)
        return err;
    err = iowner(ip);
    if (err == 0) {
        ip->d.mode = (ip->d.mode & S_IFMT) | (tf->ecx & 07777);
        ip->d.ctime = time_now();
        iupdate(ip);
    }
    iput(ip);
    return err;
}

/* chown(path, uid, gid) */
int32_t sys_chown(struct trapframe *tf)
{
    struct inode *ip;
    int err = suser();              /* giving files away: root only (V7) */
    if (err == 0)
        err = user_namei((const char *)tf->ebx, &ip);
    if (err < 0)
        return err;
    ip->d.uid = tf->ecx;
    ip->d.gid = tf->edx;
    ip->d.ctime = time_now();
    iupdate(ip);
    iput(ip);
    return 0;
}

/* sync(): write all modified buffers to disk. Our buffer cache and inodes
 * write through, so there is never anything to do. */
int32_t sys_sync(struct trapframe *tf)
{
    (void)tf;
    return 0;
}

/* time(): seconds since 1970. (V7 libc's time(&t) stores it in t too.) */
int32_t sys_time(struct trapframe *tf)
{
    (void)tf;
    return time_now();
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
    int r = file_write(f, buf, n);
    if (r == -EPIPE)
        proc_exit(SIGPIPE);     /* SIGPIPE's default action; lesson 26 sends a real signal */
    return r;
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
    if (f->pipe || (f->ip->d.mode & S_IFMT) == S_IFCHR)
        return -ESPIPE;

    int32_t base = whence == 0 ? 0 : whence == 1 ? (int32_t)f->offset
                 : whence == 2 ? (int32_t)(IS_DIR(f->ip) ? dir_size_v7(f->ip) : f->ip->d.size)
                 : -1;
    if (base < 0 || base + off < 0)
        return -EINVAL;
    f->offset = base + off;
    return f->offset;
}

/* dup(fd): a second fd for the same open file (shared offset), the
 * lowest free one.
 * dup(fd | DUPFLG, fd2): make fd2 that second fd, closing it first if
 * needed. That's dup2(fd, fd2): V7 squeezed it into the same call. */
int32_t sys_dup(struct trapframe *tf)
{
    int fd = tf->ebx & ~DUPFLG;
    struct file *f = fd_file(fd);
    if (!f)
        return -EBADF;

    if (!(tf->ebx & DUPFLG)) {
        int fd2 = fd_alloc(f);
        if (fd2 >= 0)
            file_dup(f);
        return fd2;
    }

    int fd2 = tf->ecx;
    if (fd2 < 0 || fd2 >= NOFILE)
        return -EBADF;
    if (fd2 != fd) {
        if (current->ofile[fd2])
            file_close(current->ofile[fd2]);
        current->ofile[fd2] = file_dup(f);
    }
    return fd2;
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
    stat_file(f, ust);
    return 0;
}

/* chdir(path) */
int32_t sys_chdir(struct trapframe *tf)
{
    struct inode *ip;
    int err = user_namei((const char *)tf->ebx, &ip);
    if (err < 0)
        return err;
    err = IS_DIR(ip) ? iaccess(ip, S_IEXEC) : -ENOTDIR;
    if (err < 0) {
        iput(ip);
        return err;
    }
    iput(current->cwd);
    current->cwd = ip;
    return 0;
}

/* pipe(): returns the read end's fd, and the write end's in EDX. */
int32_t sys_pipe(struct trapframe *tf)
{
    struct pipe *p = pipe_alloc();
    if (!p)
        return -ENFILE;

    struct file *rf = file_open_pipe(p, FREAD);
    struct file *wf = file_open_pipe(p, FWRITE);
    if (!rf || !wf) {               /* file table full: undo */
        if (rf) file_close(rf); else pipe_close(p, 0);
        if (wf) file_close(wf); else pipe_close(p, 1);
        return -ENFILE;
    }

    int rfd = fd_alloc(rf);
    int wfd = rfd < 0 ? -EMFILE : fd_alloc(wf);
    if (wfd < 0) {                  /* fd table full: undo */
        if (rfd >= 0)
            current->ofile[rfd] = NULL;
        file_close(rf);
        file_close(wf);             /* the last close frees the pipe */
        return -EMFILE;
    }
    tf->edx = wfd;
    return rfd;
}

/* ioctl(fd, request, arg): device-specific control (terminal modes). */
int32_t sys_ioctl(struct trapframe *tf)
{
    struct file *f = fd_file(tf->ebx);
    if (!f)
        return -EBADF;
    return file_ioctl(f, tf->ecx, (void *)tf->edx);
}

/* stime(t): set the clock. Root only. (Not written to the CMOS clock:
 * it's back to the real time at the next boot.) */
int32_t sys_stime(struct trapframe *tf)
{
    int err = suser();
    if (err == 0)
        time_set(tf->ebx);
    return err;
}

/* access(path, mode): could the REAL user read (4), write (2), execute (1)
 * the file? mode 0: does it exist? Setuid programs use it to check what
 * the person running them may do. */
int32_t sys_access(struct trapframe *tf)
{
    int mode = tf->ecx;
    int uid = current->uid, gid = current->gid;
    struct inode *ip;

    current->uid = current->ruid;   /* check as the real user */
    current->gid = current->rgid;
    int err = user_namei((const char *)tf->ebx, &ip);
    if (err == 0) {
        if (mode & 4)
            err = iaccess(ip, S_IREAD);
        if (err == 0 && (mode & 2))
            err = iaccess(ip, S_IWRITE);
        if (err == 0 && (mode & 1))
            err = iaccess(ip, S_IEXEC);
        iput(ip);
    }
    current->uid = uid;
    current->gid = gid;
    return err;
}

/* utime(path, times): set the access and modification times.
 * times points to two time_t: { atime, mtime }. */
int32_t sys_utime(struct trapframe *tf)
{
    uint32_t *times = (uint32_t *)tf->ecx;
    struct inode *ip;

    if (!user_ok(times, 2 * sizeof(uint32_t), 0))
        return -EFAULT;
    int err = user_namei((const char *)tf->ebx, &ip);
    if (err < 0)
        return err;
    err = iowner(ip);
    if (err == 0) {
        ip->d.atime = times[0];
        ip->d.mtime = times[1];
        ip->d.ctime = time_now();
        iupdate(ip);
    }
    iput(ip);
    return err;
}
