/* sys/param.h -- the constants V7 programs take from the kernel's
 * param.h. (V7's BSIZE is 512: ls -s and du count 512-byte blocks.) */
#ifndef _SYS_PARAM_H
#define _SYS_PARAM_H

#include <sys/types.h>          /* V7's param.h repeated the types and major() */

#define NOFILE  20              /* open files per process */
#define NCARGS  4096            /* bytes of exec arguments (one page) */
#define CANBSIZ 256             /* longest terminal line */
#define HZ      100             /* clock ticks per second */
#define NSIG    17

#define BSIZE   512             /* the V7 block, for counting */
#define BSHIFT  9               /* log2(BSIZE) */
#define BMASK   0777
#define NBPW    sizeof(int)

#define CMASK   0               /* default umask */
#define NODEV   ((dev_t)-1)
#define ROOTINO ((ino_t)2)      /* inode number of every root directory */

#endif
