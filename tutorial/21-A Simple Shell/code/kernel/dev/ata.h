/* ata.h -- the primary master IDE/ATA disk, in PIO mode. */
#ifndef DEV_ATA_H
#define DEV_ATA_H

#include <stdint.h>

#define SECTOR_SIZE 512

void ata_init(void);

/* Read/write `count` sectors starting at sector `lba`.
 * Returns 0, or -1 on a disk error. */
int ata_read(uint32_t lba, uint32_t count, void *buf);
int ata_write(uint32_t lba, uint32_t count, const void *buf);

#endif
