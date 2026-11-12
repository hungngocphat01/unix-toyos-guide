/* signal.h -- V7's signals. Numbers in abi/signal.h. */
#ifndef _SIGNAL_H
#define _SIGNAL_H

#include <sys/cdefs.h>
#include "abi/signal.h"

#define SIG_DFL ((int (*)())0)      /* default action: usually die */
#define SIG_IGN ((int (*)())1)      /* ignore */
#define BADSIG  ((int (*)())-1)     /* signal() failed */

int (*signal __P((int, int (*)())))();
int kill __P((int, int));

#endif
