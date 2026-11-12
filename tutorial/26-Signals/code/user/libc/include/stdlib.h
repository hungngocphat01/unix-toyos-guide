/* stdlib.h -- not in V7 either; collects what modern code expects. */
#ifndef _STDLIB_H
#define _STDLIB_H

#include <sys/cdefs.h>

char  *malloc __P((unsigned));              /* V7: char *, not void * */
char  *calloc __P((unsigned, unsigned));
char  *realloc __P((char *, unsigned));
void   free __P((char *));

int    atoi __P((const char *));
long   atol __P((const char *));
double atof __P((const char *));
int    abs __P((int));
int    rand __P((void));
void   srand __P((unsigned));
void   qsort __P((char *, unsigned, int, int (*)()));
char  *getenv __P((const char *));
int    system __P((const char *));             /* run a command with sh -c */
char  *mktemp __P((char *));

void   exit __P((int)) __attribute__((noreturn));
void   abort __P((void)) __attribute__((noreturn));

#endif
