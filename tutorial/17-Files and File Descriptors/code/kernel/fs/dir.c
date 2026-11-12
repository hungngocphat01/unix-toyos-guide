/* dir.c -- directories and path names. */
#include "fs/fs.h"
#include "fs/bio.h"
#include "lib/string.h"
#include "abi/dir.h"
#include "proc/proc.h"

struct lookup {
    const char *name;
    int         len;
    uint32_t    inum;           /* result: 0 = not found */
};

static int match_name(struct ext2_dirent *de, void *arg)
{
    struct lookup *l = arg;
    if (de->name_len == l->len && memcmp(de->name, l->name, l->len) == 0) {
        l->inum = de->inode;
        return 1;               /* found: stop */
    }
    return 0;
}

static void dir_foreach(struct inode *dp,
                        int (*fn)(struct ext2_dirent *de, void *arg), void *arg);

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

/* "/usr/bin/ls" -> the inode of ls. Paths not starting with '/' start
 * at the current directory. Returns NULL if not found. */
struct inode *namei(const char *path)
{
    struct inode *ip;
    if (*path == '/' || !current || !current->cwd)
        ip = iget(rootdev, EXT2_ROOT_INO);
    else
        ip = idup(current->cwd);

    while (*path) {
        while (*path == '/')                /* skip slashes */
            path++;
        if (*path == '\0')
            break;

        const char *name = path;            /* next component */
        while (*path && *path != '/')
            path++;
        int len = path - name;
        if (len > NAME_MAX) {
            iput(ip);
            return NULL;
        }

        struct inode *next = dirlookup(ip, name, len);
        iput(ip);
        if (!next)
            return NULL;
        ip = next;
    }
    return ip;
}

/* Call fn(entry, arg) for each used entry of directory dp, in order,
 * until fn returns non-zero. */
static void dir_foreach(struct inode *dp,
                        int (*fn)(struct ext2_dirent *de, void *arg), void *arg)
{
    for (uint32_t off = 0; off < dp->d.size; off += BLOCK_SIZE) {
        uint32_t blockno = bmap(dp, off / BLOCK_SIZE);
        if (blockno == 0)
            continue;
        struct buf *b = bread(dp->dev, blockno);
        for (uint32_t pos = 0; pos < BLOCK_SIZE; ) {
            struct ext2_dirent *de = (struct ext2_dirent *)(b->data + pos);
            if (de->rec_len == 0)
                break;
            if (de->inode != 0 && fn(de, arg)) {
                brelse(b);
                return;
            }
            pos += de->rec_len;
        }
        brelse(b);
    }
}

struct v7_reader {
    uint32_t first;             /* index of the first entry wanted */
    uint32_t count;             /* how many entries fit in the caller's buffer */
    uint32_t index;             /* index of the current entry */
    struct direct *out;
    uint32_t done;              /* entries produced */
};

static int emit_v7(struct ext2_dirent *de, void *arg)
{
    struct v7_reader *r = arg;
    if (r->index++ < r->first)
        return 0;               /* before the current offset */

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
