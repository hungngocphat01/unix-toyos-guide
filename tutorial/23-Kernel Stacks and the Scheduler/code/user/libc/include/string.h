/* string.h -- not in V7 (programs declared these themselves); for modern code. */
#ifndef _STRING_H
#define _STRING_H

#include <sys/cdefs.h>
#include <stddef.h>

char  *strcpy __P((char *, const char *));
char  *strncpy __P((char *, const char *, int));
char  *strcat __P((char *, const char *));
char  *strncat __P((char *, const char *, int));
int    strcmp __P((const char *, const char *));
int    strncmp __P((const char *, const char *, int));
int    strlen __P((const char *));
char  *index __P((const char *, int));      /* V7's name for strchr  */
char  *rindex __P((const char *, int));     /* V7's name for strrchr */

/* Not V7, but GCC may call these itself (struct copies, = {0}). */
void  *memcpy __P((void *, const void *, size_t));
void  *memmove __P((void *, const void *, size_t));
void  *memset __P((void *, int, size_t));
int    memcmp __P((const void *, const void *, size_t));

#endif
