/* ata.c -- ATA disk driver, PIO mode, polling, LBA28.
 *
 * "PIO" = the CPU copies every 16-bit word through the data port itself
 * (no DMA). "Polling" = we spin on the status register instead of
 * waiting for IRQ 14 (lesson 24 makes it interrupt-driven).
 */
#include "dev/ata.h"
#include "arch/i386/io.h"
#include "lib/kprintf.h"

/* Primary bus I/O ports */
#define ATA_DATA        0x1F0   /* 16-bit data port                       */
#define ATA_ERROR       0x1F1
#define ATA_COUNT       0x1F2   /* number of sectors                      */
#define ATA_LBA_LOW     0x1F3   /* LBA bits 0..7                          */
#define ATA_LBA_MID     0x1F4   /* LBA bits 8..15                         */
#define ATA_LBA_HIGH    0x1F5   /* LBA bits 16..23                        */
#define ATA_DRIVE       0x1F6   /* 0xE0 = master + LBA mode | LBA 24..27  */
#define ATA_STATUS      0x1F7   /* read: status                           */
#define ATA_COMMAND     0x1F7   /* write: command                         */
#define ATA_CONTROL     0x3F6   /* write: device control; read: alt status */

/* Status bits */
#define ST_ERR  0x01            /* error                                  */
#define ST_DRQ  0x08            /* data request: ready to transfer data   */
#define ST_DF   0x20            /* drive fault                            */
#define ST_BSY  0x80            /* busy: ignore every other bit           */

/* Commands */
#define CMD_READ     0x20
#define CMD_WRITE    0x30
#define CMD_FLUSH    0xE7
#define CMD_IDENTIFY 0xEC

#define CTRL_NIEN    0x02       /* don't raise IRQ 14 */

static int present;

/* Reading the status port 4 times takes ~400ns: the time the drive needs
 * after a command before its status is meaningful. */
static void delay400ns(void)
{
    for (int i = 0; i < 4; i++)
        inb(ATA_CONTROL);
}

/* Wait until not busy. If want_drq, also wait for DRQ. Returns 0 or -1. */
static int wait_ready(int want_drq)
{
    uint8_t st;
    while ((st = inb(ATA_STATUS)) & ST_BSY)
        ;
    if (st & (ST_ERR | ST_DF))
        return -1;
    if (want_drq && !(st & ST_DRQ))
        return -1;
    return 0;
}

/* Select the drive and send a command with an LBA and a sector count. */
static void send_command(uint8_t cmd, uint32_t lba, uint8_t count)
{
    outb(ATA_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    delay400ns();
    outb(ATA_COUNT, count);
    outb(ATA_LBA_LOW, lba & 0xFF);
    outb(ATA_LBA_MID, (lba >> 8) & 0xFF);
    outb(ATA_LBA_HIGH, (lba >> 16) & 0xFF);
    outb(ATA_COMMAND, cmd);
    delay400ns();
}

void ata_init(void)
{
    outb(ATA_CONTROL, CTRL_NIEN);

    outb(ATA_DRIVE, 0xE0);
    delay400ns();
    outb(ATA_COMMAND, CMD_IDENTIFY);
    delay400ns();
    if (inb(ATA_STATUS) == 0) {         /* nothing on the bus */
        kprintf("ata: no disk\n");
        return;
    }
    uint16_t id[256];
    if (wait_ready(1) < 0) {
        kprintf("ata: IDENTIFY failed\n");
        return;
    }
    insw(ATA_DATA, id, 256);

    /* Words 27..46: model name, two characters per word, swapped. */
    char model[41];
    for (int i = 0; i < 20; i++) {
        model[2 * i] = id[27 + i] >> 8;
        model[2 * i + 1] = id[27 + i] & 0xFF;
    }
    model[40] = '\0';
    for (int i = 39; i >= 0 && model[i] == ' '; i--)
        model[i] = '\0';

    /* Words 60..61: number of sectors addressable with LBA28. */
    uint32_t sectors = id[60] | ((uint32_t)id[61] << 16);
    kprintf("ata: \"%s\", %lu sectors (%lu KiB)\n", model, sectors, sectors / 2);
    present = 1;
}

int ata_read(uint32_t lba, uint32_t count, void *buf)
{
    if (!present)
        return -1;
    uint16_t *p = buf;
    send_command(CMD_READ, lba, count);
    for (uint32_t i = 0; i < count; i++) {
        if (wait_ready(1) < 0)
            return -1;
        insw(ATA_DATA, p, SECTOR_SIZE / 2);
        p += SECTOR_SIZE / 2;
    }
    return 0;
}

int ata_write(uint32_t lba, uint32_t count, const void *buf)
{
    if (!present)
        return -1;
    const uint16_t *p = buf;
    send_command(CMD_WRITE, lba, count);
    for (uint32_t i = 0; i < count; i++) {
        if (wait_ready(1) < 0)
            return -1;
        outsw(ATA_DATA, p, SECTOR_SIZE / 2);
        p += SECTOR_SIZE / 2;
    }
    /* Make sure the data left the drive's cache. */
    outb(ATA_COMMAND, CMD_FLUSH);
    return wait_ready(0);
}
