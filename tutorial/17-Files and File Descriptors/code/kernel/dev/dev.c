/* dev.c -- the device switch tables: major number -> driver functions. */
#include "dev/dev.h"
#include "dev/ata.h"
#include "dev/console.h"

/* One BLOCK_SIZE block = 2 sectors. (We only have one disk: minor ignored.) */
static int ata_block_read(int minor, uint32_t blockno, void *buf)
{
    (void)minor;
    return ata_read(blockno * (BLOCK_SIZE / SECTOR_SIZE), BLOCK_SIZE / SECTOR_SIZE, buf);
}

static int ata_block_write(int minor, uint32_t blockno, const void *buf)
{
    (void)minor;
    return ata_write(blockno * (BLOCK_SIZE / SECTOR_SIZE), BLOCK_SIZE / SECTOR_SIZE, buf);
}

struct bdevsw bdevsw[] = {
    [BDEV_ATA] = { ata_block_read, ata_block_write },
};
int nbdev = sizeof(bdevsw) / sizeof(bdevsw[0]);

/* ---- character devices ---- */

static int console_read(int minor, void *buf, uint32_t n)
{
    (void)minor; (void)buf; (void)n;
    return 0;                       /* end of file: lesson 18 adds the TTY */
}

static int console_write(int minor, const void *buf, uint32_t n)
{
    (void)minor;
    const char *s = buf;
    for (uint32_t i = 0; i < n; i++)
        console_putc(s[i]);
    return n;
}

/* /dev/null: reads see end of file, writes vanish. */
static int null_read(int minor, void *buf, uint32_t n)
{
    (void)minor; (void)buf; (void)n;
    return 0;
}

static int null_write(int minor, const void *buf, uint32_t n)
{
    (void)minor; (void)buf;
    return n;
}

struct cdevsw cdevsw[] = {
    [CDEV_CONSOLE] = { console_read, console_write },
    [CDEV_NULL]    = { null_read,    null_write },
};
int ncdev = sizeof(cdevsw) / sizeof(cdevsw[0]);
