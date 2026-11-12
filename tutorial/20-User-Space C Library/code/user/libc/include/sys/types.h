/* sys/types.h -- V7's types. ino_t, dev_t, off_t and time_t are shared
 * with the kernel (abi/types.h). */
#ifndef _SYS_TYPES_H
#define _SYS_TYPES_H

#include "abi/types.h"

typedef long   daddr_t;         /* disk address */
typedef char  *caddr_t;         /* memory address */
typedef int    label_t[6];

#define major(x)      ((int)(((unsigned)(x) >> 8) & 0377))
#define minor(x)      ((int)((x) & 0377))
#define makedev(x, y) ((dev_t)(((x) << 8) | (y)))

#endif
