/* dir.c -- directories and path names.
 *
 * An ext2 directory block is a chain of variable-length entries; rec_len
 * leads from one to the next, and the last one stretches to the end of
 * the block. An entry with inode 0 is unused.
 */
#include "fs/fs.h"
#include "fs/bio.h"
#include "lib/string.h"
#include "abi/dir.h"
#include "abi/errno.h"
#include "proc/proc.h"

/* Bytes needed by an entry with a name of len bytes (a multiple of 4). */
#define DIRENT_SIZE(len) ((8 + (len) + 3) & ~3u)

/* Called for each entry (used or not) of a directory block; prev is the
 * entry before it in the same block (NULL for the first). Return non-zero
 * to stop. If you change b->data, bwrite(b). */
typedef int (*dirent_fn)(struct buf *b, struct ext2_dirent *prev,
                         struct ext2_dirent *de, void *arg);

/* Call fn for each entry of directory dp, in order. Returns 1 if fn
 * stopped the walk, 0 if it reached the end. */
static int dir_foreach(struct inode *dp, dirent_fn fn, void *arg)
{
    for (uint32_t off = 0; off < dp->d.size; off += BLOCK_SIZE) {
        uint32_t blockno = bmap(dp, off / BLOCK_SIZE, 0);
        if (blockno == 0)
            continue;
        struct buf *b = bread(dp->dev, blockno);
        struct ext2_dirent *prev = NULL;
        for (uint32_t pos = 0; pos < BLOCK_SIZE; ) {
            struct ext2_dirent *de = (struct ext2_dirent *)(b->data + pos);
            if (de->rec_len == 0)
                break;                      /* corrupt: avoid looping forever */
            if (fn(b, prev, de, arg)) {
                brelse(b);
                return 1;
            }
            prev = de;
            pos += de->rec_len;
        }
        brelse(b);
    }
    return 0;
}

/* ---- looking up ---- */

struct lookup {
    const char *name;
    int         len;
    uint32_t    inum;               /* result: 0 = not found */
};

static int match_name(struct buf *b, struct ext2_dirent *prev,
                      struct ext2_dirent *de, void *arg)
{
    (void)b; (void)prev;
    struct lookup *l = arg;
    if (de->inode && de->name_len == l->len && memcmp(de->name, l->name, l->len) == 0) {
        l->inum = de->inode;
        return 1;
    }
    return 0;
}

/* Look for `name` (len bytes, not NUL-terminated) in directory dp.
 * Returns its inode (refcnt++), or NULL. */
struct inode *dirlookup(struct inode *dp, const char *name, int len)
{
    struct lookup l = { name, len, 0 };

    if (!IS_DIR(dp))
        return NULL;
    dir_foreach(dp, match_name, &l);
    return l.inum ? iget(dp->dev, l.inum) : NULL;
}

/* ---- adding an entry ---- */

static uint8_t file_type(struct inode *ip)
{
    switch (ip->d.mode & S_IFMT) {
    case S_IFREG: return EXT2_FT_REG;
    case S_IFDIR: return EXT2_FT_DIR;
    case S_IFCHR: return EXT2_FT_CHRDEV;
    case S_IFBLK: return EXT2_FT_BLKDEV;
    }
    return EXT2_FT_UNKNOWN;
}

static void fill_entry(struct ext2_dirent *de, const char *name, int len, struct inode *ip)
{
    de->inode = ip->inum;
    de->name_len = len;
    de->file_type = file_type(ip);
    memcpy(de->name, name, len);
}

struct add {
    const char   *name;
    int           len;
    struct inode *ip;
};

/* Put the new entry in de's slack: an unused entry big enough, or the
 * space after a used entry's name:
 *
 *   before: [ inode=12 "bin" ......... rec_len=1000 ......... ]
 *   after:  [ inode=12 "bin" 12 ][ inode=40 "new" rec_len=988 ]
 */
static int add_here(struct buf *b, struct ext2_dirent *prev,
                    struct ext2_dirent *de, void *arg)
{
    (void)prev;
    struct add *a = arg;
    uint32_t need = DIRENT_SIZE(a->len);
    uint32_t used = de->inode ? DIRENT_SIZE(de->name_len) : 0;

    if (de->rec_len - used < need)
        return 0;
    if (used) {                             /* split de in two */
        struct ext2_dirent *next = (struct ext2_dirent *)((char *)de + used);
        next->rec_len = de->rec_len - used;
        de->rec_len = used;
        de = next;
    }
    fill_entry(de, a->name, a->len, a->ip);
    bwrite(b);
    return 1;
}

/* Add the entry name -> ip to directory dp. (The caller updates ip's
 * link count.) Returns 0 or -errno. */
int dirlink(struct inode *dp, const char *name, int len, struct inode *ip)
{
    struct add a = { name, len, ip };

    if (len > NAME_MAX)
        return -ENOENT;
    if (dir_foreach(dp, add_here, &a))
        return 0;

    /* No room: add a block to the directory, holding just this entry. */
    uint32_t blockno = bmap(dp, dp->d.size / BLOCK_SIZE, 1);
    if (blockno == 0)
        return -ENOSPC;
    struct buf *b = bread(dp->dev, blockno);
    struct ext2_dirent *de = (struct ext2_dirent *)b->data;
    de->rec_len = BLOCK_SIZE;
    fill_entry(de, name, len, ip);
    bwrite(b);
    brelse(b);
    dp->d.size += BLOCK_SIZE;
    iupdate(dp);
    return 0;
}

/* ---- removing an entry ---- */

/* Remove de by merging it into the previous entry, or, if it's the first
 * of its block, by marking it unused. */
static int remove_here(struct buf *b, struct ext2_dirent *prev,
                       struct ext2_dirent *de, void *arg)
{
    struct lookup *l = arg;
    if (!match_name(b, prev, de, l))
        return 0;
    if (prev)
        prev->rec_len += de->rec_len;
    else
        de->inode = 0;
    bwrite(b);
    return 1;
}

/* Remove the entry `name` from dp. (The caller updates the link count.) */
int dirunlink(struct inode *dp, const char *name, int len)
{
    struct lookup l = { name, len, 0 };
    return dir_foreach(dp, remove_here, &l) ? 0 : -ENOENT;
}

/* ---- path names ---- */

/* Find the next component of path: skip slashes, set *name and *len.
 * Returns the rest of the path after it, or NULL if there is none. */
static const char *next_name(const char *path, const char **name, int *len)
{
    while (*path == '/')
        path++;
    if (*path == '\0')
        return NULL;
    *name = path;
    while (*path && *path != '/')
        path++;
    *len = path - *name;
    return path;
}

/* Walk path from the root or the current directory. With parent = 0,
 * return the inode it names. With parent = 1, stop one component early:
 * return the directory, and the last component in *last, *lastlen.
 * On failure, return NULL and set *err. */
static struct inode *walk(const char *path, int parent, const char **last,
                          int *lastlen, int *err)
{
    struct inode *ip;
    const char *name, *dummy;
    int len, dummylen;
    int e = -ENOENT;

    if (*path == '/' || !current || !current->cwd)
        ip = iget(rootdev, EXT2_ROOT_INO);
    else
        ip = idup(current->cwd);

    while ((path = next_name(path, &name, &len)) != NULL) {
        if (len > NAME_MAX)
            break;
        if (!IS_DIR(ip)) {
            e = -ENOTDIR;
            break;
        }
        if (current && iaccess(ip, S_IEXEC) < 0) {
            e = -EACCES;                    /* may not search this directory */
            break;
        }
        if (parent && !next_name(path, &dummy, &dummylen)) {
            *last = name;                   /* the last component */
            *lastlen = len;
            return ip;
        }
        struct inode *next = dirlookup(ip, name, len);
        iput(ip);
        if (!next) {
            ip = NULL;
            break;
        }
        ip = next;
    }
    if (ip && !path && !parent)
        return ip;                          /* walked the whole path */
    if (ip)
        iput(ip);
    if (err)
        *err = e;
    return NULL;
}

/* "/usr/bin/ls" -> the inode of ls. Paths not starting with '/' start
 * at the current directory. */
struct inode *namei(const char *path, int *err)
{
    return walk(path, 0, NULL, NULL, err);
}

struct inode *namei_parent(const char *path, const char **name, int *len, int *err)
{
    return walk(path, 1, name, len, err);
}

/* ---- reading a directory as V7 entries ---- */

struct v7_reader {
    uint32_t first;             /* index of the first entry wanted */
    uint32_t count;             /* how many entries fit in the caller's buffer */
    uint32_t index;             /* index of the current entry */
    struct direct *out;
    uint32_t done;              /* entries produced */
};

static int emit_v7(struct buf *b, struct ext2_dirent *prev,
                   struct ext2_dirent *de, void *arg)
{
    (void)b; (void)prev;
    struct v7_reader *r = arg;
    if (de->inode == 0 || r->index++ < r->first)
        return 0;               /* unused, or before the current offset */

    struct direct *d = &r->out[r->done++];
    d->d_ino = de->inode;       /* fits: small disks have < 65536 inodes */
    memset(d->d_name, 0, DIRSIZ);
    memcpy(d->d_name, de->name, de->name_len < DIRSIZ ? de->name_len : DIRSIZ);
    return r->done == r->count; /* stop when the buffer is full */
}

/* V7 programs (ls, pwd, find...) read() directories and expect 16-byte
 * entries. ext2's entries look different, so we translate: entry i of the
 * V7 view is the i-th used ext2 entry, at offset i * 16. Names longer than
 * 14 characters are cut. */
int dir_read_v7(struct inode *dp, void *dst, uint32_t off, uint32_t n)
{
    struct v7_reader r = {
        .first = off / sizeof(struct direct),
        .count = n / sizeof(struct direct),
        .out = dst,
    };
    if (r.count == 0)
        return 0;
    dir_foreach(dp, emit_v7, &r);
    return r.done * sizeof(struct direct);
}
