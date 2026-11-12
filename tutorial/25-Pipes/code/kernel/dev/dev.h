/* dev.h -- device numbers and the block device switch (V7: conf.c).
 *
 * A device number packs a MAJOR number (which driver) and a MINOR number
 * (which unit of that driver, e.g. which disk or which terminal).
 */
#ifndef DEV_DEV_H
#define DEV_DEV_H

#include <stdint.h>
#include "abi/types.h"              /* dev_t */

#define MAJOR(d)      ((d) >> 8)
#define MINOR(d)      ((d) & 0xFF)
#define MKDEV(ma, mi) ((dev_t)(((ma) << 8) | (mi)))

/* Block devices transfer whole blocks of BLOCK_SIZE bytes. */
#define BLOCK_SIZE 1024

struct bdevsw {
    int (*read)(int minor, uint32_t blockno, void *buf);
    int (*write)(int minor, uint32_t blockno, const void *buf);
};

extern struct bdevsw bdevsw[];
extern int nbdev;

/* Character devices transfer any number of bytes (terminals, /dev/null). */
struct cdevsw {
    int (*read)(int minor, void *buf, uint32_t n);
    int (*write)(int minor, const void *buf, uint32_t n);
    int (*ioctl)(int minor, uint32_t cmd, void *arg);  /* NULL: not a tty */
};

extern struct cdevsw cdevsw[];
extern int ncdev;

/* Major numbers of our character devices (/dev entries: tools/mkdisk.sh) */
#define CDEV_CONSOLE 0
#define CDEV_NULL    1

/* Major numbers of our block devices */
#define BDEV_ATA 0

#define ROOTDEV MKDEV(BDEV_ATA, 0)      /* the disk "/" lives on */

#endif
