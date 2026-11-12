/* sgtty.h -- terminal modes (V7). */
#ifndef _SGTTY_H
#define _SGTTY_H

#include <sys/cdefs.h>
#include "abi/sgtty.h"

int ioctl __P((int, int, void *));
int stty __P((int, struct sgttyb *));
int gtty __P((int, struct sgttyb *));

#endif
