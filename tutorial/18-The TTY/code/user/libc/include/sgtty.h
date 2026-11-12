/* sgtty.h -- terminal modes (V7). */
#ifndef _SGTTY_H
#define _SGTTY_H

#include "abi/sgtty.h"

int ioctl(int fd, int request, void *arg);
int stty(int fd, struct sgttyb *mode);
int gtty(int fd, struct sgttyb *mode);

#endif
