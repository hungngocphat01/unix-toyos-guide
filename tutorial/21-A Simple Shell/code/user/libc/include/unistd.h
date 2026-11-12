/* unistd.h -- system call wrappers. Not a V7 header (V7 programs declared
 * these themselves, which still works), but modern code expects it. */
#ifndef _UNISTD_H
#define _UNISTD_H

#include <sys/cdefs.h>

void  _exit __P((int)) __attribute__((noreturn));
void  exit __P((int)) __attribute__((noreturn));
int   fork __P((void));
int   wait __P((int *));
int   getpid __P((void));
int   execv __P((const char *, char **));
int   execve __P((const char *, char **, char **));
int   execl __P((const char *, const char *, ...));
int   execle __P((const char *, const char *, ...));
int   execvp __P((const char *, char **));      /* searches $PATH */
int   brk __P((char *));
char *sbrk __P((int));

int   read __P((int, void *, unsigned));
int   write __P((int, const void *, unsigned));
int   open __P((const char *, int));            /* 0 read, 1 write, 2 both */
int   close __P((int));
long  lseek __P((int, long, int));
int   dup __P((int));
int   creat __P((const char *, int));
int   mknod __P((const char *, int, int));
int   link __P((const char *, const char *));
int   unlink __P((const char *));
int   chdir __P((const char *));
int   chmod __P((const char *, int));
int   chown __P((const char *, int, int));
int   ioctl __P((int, int, void *));
void  sync __P((void));
int   isatty __P((int));
unsigned sleep __P((unsigned));
char *ttyname __P((int));

extern char **environ;

#endif
