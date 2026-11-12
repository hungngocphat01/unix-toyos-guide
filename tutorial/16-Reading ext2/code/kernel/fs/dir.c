/* dir.c -- directories and path names. */
#include "fs/fs.h"
#include "fs/bio.h"
#include "lib/string.h"

/* Look for `name` (len bytes, not NUL-terminated) in directory dp.
 * Returns its inode (refcnt++), or NULL. */
struct inode *dirlookup(struct inode *dp, const char *name, int len)
{
    if (!IS_DIR(dp))
        return NULL;

    for (uint32_t off = 0; off < dp->d.size; off += BLOCK_SIZE) {
        uint32_t blockno = bmap(dp, off / BLOCK_SIZE);
        if (blockno == 0)
            continue;
        struct buf *b = bread(dp->dev, blockno);

        /* Entries never cross a block boundary; rec_len chains them. */
        for (uint32_t pos = 0; pos < BLOCK_SIZE; ) {
            struct ext2_dirent *de = (struct ext2_dirent *)(b->data + pos);
            if (de->rec_len == 0)
                break;                      /* corrupt: avoid looping forever */
            if (de->inode != 0 && de->name_len == len &&
                memcmp(de->name, name, len) == 0) {
                uint32_t inum = de->inode;
                brelse(b);
                return iget(dp->dev, inum);
            }
            pos += de->rec_len;
        }
        brelse(b);
    }
    return NULL;
}

/* "/usr/bin/ls" -> the inode of ls. Every path is absolute for now
 * (lesson 17 adds the current directory). Returns NULL if not found. */
struct inode *namei(const char *path)
{
    struct inode *ip = iget(rootdev, EXT2_ROOT_INO);

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
