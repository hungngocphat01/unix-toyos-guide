/* unistd.h -- system call wrappers (not a V7 header; V7 programs declared
 * these themselves, which still works). */
#ifndef _UNISTD_H
#define _UNISTD_H

void  _exit(int status) __attribute__((noreturn));
void  exit(int status) __attribute__((noreturn));
int   write(int fd, const void *buf, unsigned n);
int   getpid(void);
int   exec(const char *path, char **argv);
int   exece(const char *path, char **argv, char **envp);
int   brk(void *addr);
void *sbrk(int incr);

extern char **environ;

#endif
