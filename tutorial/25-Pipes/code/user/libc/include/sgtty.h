/* sgtty.h -- terminal modes (V7). */
#ifndef _SGTTY_H
#define _SGTTY_H

#include <sys/cdefs.h>
#include "abi/sgtty.h"

/* The rest of V7's <sgtty.h>, so that V7 programs (stty) compile. toyos
 * ignores speeds, parity and delays: they mattered for real terminals. */
#define TANDEM   01
#define LCASE    04
#define ODDP     0100
#define EVENP    0200
#define ANYP     0300
#define NLDELAY  001400
#define TBDELAY  006000
#define XTABS    006000
#define CRDELAY  030000
#define VTDELAY  040000
#define BSDELAY  0100000
#define ALLDELAY 0177400
#define CR0 0
#define CR1 010000
#define CR2 020000
#define CR3 030000
#define NL0 0
#define NL1 000400
#define NL2 001000
#define NL3 001400
#define TAB0 0
#define TAB1 002000
#define TAB2 004000
#define FF0 0
#define FF1 040000
#define BS0 0
#define BS1 0100000

#define B0    0
#define B50   1
#define B75   2
#define B110  3
#define B134  4
#define B150  5
#define B200  6
#define B300  7
#define B600  8
#define B1200 9
#define B1800 10
#define B2400 11
#define B4800 12
#define B9600 13
#define EXTA  14
#define EXTB  15

#define TIOCGETD (('t' << 8) | 0)
#define TIOCSETD (('t' << 8) | 1)
#define TIOCHPCL (('t' << 8) | 2)   /* hang up on last close */
#define TIOCEXCL (('t' << 8) | 13)
#define TIOCNXCL (('t' << 8) | 14)
#define FIOCLEX  (('f' << 8) | 1)   /* close this fd on exec */
#define FIONCLEX (('f' << 8) | 2)

int ioctl __P((int, int, void *));
int stty __P((int, struct sgttyb *));
int gtty __P((int, struct sgttyb *));

#endif
