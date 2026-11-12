/* abi/types.h -- basic types shared by the kernel and user programs.
 * Sizes chosen so that V7's structs keep their V7 layout on i386
 * (e.g. struct direct is 16 bytes, as V7 programs expect). */
#ifndef ABI_TYPES_H
#define ABI_TYPES_H

typedef unsigned short ino_t;   /* inode number */
typedef unsigned short dev_t;   /* device number: major << 8 | minor */
typedef long           off_t;   /* offset in a file */
typedef long           time_t;  /* seconds since 1970-01-01 00:00 UTC */

#endif
