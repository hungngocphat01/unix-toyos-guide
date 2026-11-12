/* abi/dir.h -- a directory entry as V7 programs see it (read() on a
 * directory returns an array of these; see kernel/fs/dir.c). */
#ifndef ABI_DIR_H
#define ABI_DIR_H

#include "abi/types.h"

#define DIRSIZ 14

struct direct {
    ino_t d_ino;                /* 0 = unused entry */
    char  d_name[DIRSIZ];       /* NOT NUL-terminated if exactly 14 chars */
};                              /* 16 bytes */

#endif
