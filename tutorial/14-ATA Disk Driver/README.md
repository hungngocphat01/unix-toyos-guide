# 14 — ATA Disk Driver

**Goal:** read and write sectors of a hard disk. Everything about files
(lessons 15–19) sits on these two functions.

**Milestone** (run it twice; the counter is stored on the disk):

```
ata: "QEMU HARDDISK", 16384 sectors (8192 KiB)
disk: sector 0 says "toyos test disk, sector 0"
disk: this is boot number 2
starting init as pid 1
...
```

and on the host:

```sh
$ xxd -s 512 -l 16 build/disk.img
00000200: 626f 6f74 733d 0000 0200 0000 0000 0000  boots=..........
```

**Reference code:** [`code/`](code/). New: [`dev/ata.h`](code/kernel/dev/ata.h),
[`dev/ata.c`](code/kernel/dev/ata.c). Changed: `insw`/`outsw` in
[`io.h`](code/kernel/arch/i386/io.h), `disk_test()` in [`kmain.c`](code/kernel/kmain.c),
the [`Makefile`](code/Makefile) creates `build/disk.img` (8 MiB) and
attaches it with `-drive file=build/disk.img,format=raw,if=ide,index=0`.

---

## Concepts

### The disk is an array of 512-byte sectors

From the OS's point of view a disk is `sector[0 .. N-1]`, each 512 bytes,
addressed by a **Logical Block Address** (LBA). We use **LBA28** (28-bit
addresses, up to 128 GiB): more than enough.

The disk image file on your Mac *is* that array: byte offset = `lba * 512`.
That's why `xxd -s 512` shows sector 1.

### Talking to an ATA (IDE) drive

The primary IDE controller lives at I/O ports `0x1F0–0x1F7` plus `0x3F6`.
Reading sectors is a little ritual:

```
 1. select the drive, LBA mode, top 4 bits of LBA   outb(0x1F6, 0xE0 | lba>>24)
 2. sector count                                    outb(0x1F2, count)
 3. LBA bits 0-7, 8-15, 16-23                       outb(0x1F3/4/5, ...)
 4. command READ SECTORS                            outb(0x1F7, 0x20)
 5. for each sector:
      wait until status BSY=0 and DRQ=1             inb(0x1F7)
      read 256 16-bit words                         rep insw from 0x1F0
```

Writing is the same with command `0x30` and `rep outsw`, plus a **cache
flush** (`0xE7`) at the end so the data really reaches the disk.

The status register (`0x1F7`):

| Bit | Name | Meaning |
|---|---|---|
| 7 | BSY | busy: **the other bits are meaningless while it's set** |
| 6 | DRDY | drive ready |
| 5 | DF | drive fault |
| 3 | DRQ | data request: ready to transfer a sector |
| 0 | ERR | error (details in `0x1F1`) |

From [`ata.c`](code/kernel/dev/ata.c):

```c
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
```

The drive needs ~400 ns after selecting a drive or sending a command before
its status is valid. Reading the alternate status port (`0x3F6`) four times
takes about that long.

### PIO, polling, and why that's fine for now

- **PIO**: the CPU moves every word itself (`rep insw`). The alternative,
  DMA, has the controller write into RAM directly; faster, more setup.
- **Polling**: we spin until the drive is ready. The alternative is IRQ 14
  ("sector ready") so the CPU can run another process meanwhile, which only
  matters once there *is* another process to run (lesson 23). Lesson 24 explains why we keep polling anyway;
  for now we set `nIEN` in the control register so the drive doesn't raise
  IRQs nobody handles.

### IDENTIFY

Command `0xEC` returns 256 words describing the drive. Useful fields:
words 27–46 = model string (each word's two bytes are **swapped**), words
60–61 = total LBA28 sectors. If the status reads `0` right after the command,
no drive is attached.

---

## Modules

| File | Responsibility | Exposes |
|---|---|---|
| [`dev/ata.c`](code/kernel/dev/ata.c) | primary-master disk, PIO, polling | `ata_init()`, `ata_read(lba, count, buf)`, `ata_write(lba, count, buf)`, `SECTOR_SIZE` |
| [`arch/i386/io.h`](code/kernel/arch/i386/io.h) | `insw`/`outsw` (`rep insw`/`rep outsw`) | |

Keep the driver dumb: it knows sectors, not blocks, files or caching. The
next lesson puts a cache in front of it, and nothing else in the kernel will
call `ata_read` directly.

## Test

- The milestone, twice. Then `make clean` (deletes the image) and see the
  counter restart.
- Write a recognizable pattern to sector 100 and check it with
  `xxd -s 51200 -l 64 build/disk.img`.
- Read 8 sectors at once and compare with 8 single reads.
- Remove `-drive` from `QEMUFLAGS`: "ata: no disk", and the kernel must
  continue (`ata_read` returns -1).

## Pitfalls

- **Reading status bits while BSY is set.** They're garbage.
- **Forgetting the flush after writing**: works in QEMU, loses data on real
  hardware at power-off.
- **Model string looks like "EQUMH RADDSI K"**: you didn't swap the bytes.
- **`count = 0` means 256 sectors** in LBA28. Never pass 0.
- **Unhandled IRQ 14 storms**: without `nIEN`, the drive raises IRQ 14 after
  every command. Harmless right now (it's masked at the PIC) but confusing
  in `-d int` logs.

## References

- OSDev: "ATA PIO Mode" (the whole ritual, with port tables)
- ATA/ATAPI-6 spec, ch. 8 (command descriptions) if you want the source.
- xv6: `ide.c` (interrupt-driven version: an exercise after lesson 24).
- V7: `usr/sys/dev/hp.c`, `rp.c` (drivers for DEC disks, same structure:
  a `strategy` routine that takes a buffer).
