/* sys/stat.h */
#ifndef _SYS_STAT_H
#define _SYS_STAT_H

#include "abi/stat.h"

int stat(const char *path, struct stat *st);
int fstat(int fd, struct stat *st);

#endif
