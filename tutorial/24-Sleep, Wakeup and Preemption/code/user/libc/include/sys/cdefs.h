/* sys/cdefs.h -- one header set for two kinds of C.
 *
 * Our own programs are modern C (-std=gnu11) and want prototypes. V7's
 * libc and programs are K&R C (-std=gnu89), which often declares library
 * functions itself, e.g.  char *sprintf();  and that conflicts with a
 * prototype like  char *sprintf(char *, const char *, ...).
 *
 * So declarations are written  char *sprintf __P((char *, const char *, ...));
 * which becomes a prototype in modern C and  char *sprintf();  in K&R C.
 * (The same trick as 4.4BSD's <sys/cdefs.h>.)
 */
#ifndef _SYS_CDEFS_H
#define _SYS_CDEFS_H

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#define __P(args) args
#else
#define __P(args) ()
#endif

#endif
