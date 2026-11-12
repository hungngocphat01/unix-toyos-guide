/* abi/sgtty.h -- terminal modes, as in V7's <sgtty.h> (the parts we support). */
#ifndef ABI_SGTTY_H
#define ABI_SGTTY_H

struct sgttyb {
    char sg_ispeed;             /* input speed (ignored)  */
    char sg_ospeed;             /* output speed (ignored) */
    char sg_erase;              /* erase-one-character key */
    char sg_kill;               /* erase-whole-line key    */
    int  sg_flags;              /* modes, below            */
};

struct tchars {
    char t_intrc;               /* interrupt: sends SIGINT        */
    char t_quitc;               /* quit: sends SIGQUIT            */
    char t_startc;              /* start output (ignored)         */
    char t_stopc;               /* stop output  (ignored)         */
    char t_eofc;                /* end of file                    */
    char t_brkc;                /* extra line delimiter (ignored) */
};

/* sg_flags */
#define CBREAK  0002            /* deliver each character at once, keep signals/echo */
#define ECHO    0010            /* echo input                     */
#define CRMOD   0020            /* input CR -> NL                 */
#define RAW     0040            /* no processing at all           */

/* ioctl requests */
#define TIOCGETP (('t' << 8) | 8)    /* get struct sgttyb                     */
#define TIOCSETP (('t' << 8) | 9)    /* set struct sgttyb, flush input         */
#define TIOCSETN (('t' << 8) | 10)   /* set struct sgttyb, keep input          */
#define TIOCSETC (('t' << 8) | 17)   /* set struct tchars                      */
#define TIOCGETC (('t' << 8) | 18)   /* get struct tchars                      */

#endif
