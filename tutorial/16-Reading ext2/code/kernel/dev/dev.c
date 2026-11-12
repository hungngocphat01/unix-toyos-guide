/* dev.c -- the device switch tables: major number -> driver functions. */
#include "dev/dev.h"
#include "dev/ata.h"

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
