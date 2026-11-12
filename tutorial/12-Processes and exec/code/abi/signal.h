/* abi/signal.h -- signal numbers, identical to V7's /usr/include/signal.h.
 * Lesson 26 implements signals; until then they only appear in wait status. */
#ifndef ABI_SIGNAL_H
#define ABI_SIGNAL_H

#define SIGHUP   1      /* hangup                       */
#define SIGINT   2      /* interrupt (Ctrl-C)           */
#define SIGQUIT  3      /* quit (Ctrl-\)                */
#define SIGILL   4      /* illegal instruction          */
#define SIGTRAP  5      /* trace trap                   */
#define SIGIOT   6      /* IOT instruction (abort)      */
#define SIGEMT   7      /* EMT instruction              */
#define SIGFPE   8      /* arithmetic exception         */
#define SIGKILL  9      /* kill, can't be caught        */
#define SIGBUS  10      /* bus error                    */
#define SIGSEGV 11      /* segmentation violation       */
#define SIGSYS  12      /* bad system call              */
#define SIGPIPE 13      /* write on a pipe, no reader   */
#define SIGALRM 14      /* alarm clock                  */
#define SIGTERM 15      /* termination request          */

#define NSIG    17

#endif
