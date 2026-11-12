/* sys/stat.h -- V7's struct stat and S_IF* (shared with the kernel). */
#ifndef _SYS_STAT_H
#define _SYS_STAT_H

#include <sys/cdefs.h>
#include "abi/stat.h"

int stat __P((const char *, struct stat *));
int fstat __P((int, struct stat *));

#endif
