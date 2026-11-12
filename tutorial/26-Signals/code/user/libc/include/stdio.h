/* stdio.h -- V7's standard I/O (usr/include/stdio.h), plus declarations. */
#ifndef _STDIO_H
#define _STDIO_H

#include <sys/cdefs.h>

#define BUFSIZ  512
#define _NFILE  20

#ifndef FILE
extern struct _iobuf {
    char *_ptr;                 /* next character in the buffer */
    int   _cnt;                 /* characters left (to read, or room to write) */
    char *_base;                /* the buffer */
    char  _flag;                /* _IO* below */
    char  _file;                /* the file descriptor */
} _iob[_NFILE];
#endif

#define _IOREAD  01
#define _IOWRT   02
#define _IONBF   04
#define _IOMYBUF 010
#define _IOEOF   020
#define _IOERR   040
#define _IOSTRG  0100
#define _IORW    0200

#ifndef NULL
#define NULL    0
#endif
#define FILE    struct _iobuf
#define EOF     (-1)

#define stdin   (&_iob[0])
#define stdout  (&_iob[1])
#define stderr  (&_iob[2])

/* getc and putc are macros: usually just a pointer move, no function call. */
#define getc(p)     (--(p)->_cnt >= 0 ? *(p)->_ptr++ & 0377 : _filbuf(p))
#define getchar()   getc(stdin)
#define putc(x, p)  (--(p)->_cnt >= 0 ? ((int)(*(p)->_ptr++ = (unsigned)(x))) \
                                       : _flsbuf((unsigned)(x), p))
#define putchar(x)  putc(x, stdout)
#define feof(p)     (((p)->_flag & _IOEOF) != 0)
#define ferror(p)   (((p)->_flag & _IOERR) != 0)
#define fileno(p)   ((p)->_file)

FILE  *fopen __P((const char *, const char *));
FILE  *freopen __P((const char *, const char *, FILE *));
FILE  *fdopen __P((int, const char *));
FILE  *popen __P((const char *, const char *));    /* run a command, read or write its stdio */
int    pclose __P((FILE *));
int    fclose __P((FILE *));
int    fflush __P((FILE *));
long   ftell __P((FILE *));
int    fseek __P((FILE *, long, int));
void   rewind __P((FILE *));
void   setbuf __P((FILE *, char *));
void   clearerr __P((FILE *));

char  *fgets __P((char *, int, FILE *));
char  *gets __P((char *));
int    fgetc __P((FILE *));
int    getw __P((FILE *));
int    ungetc __P((int, FILE *));
int    fread __P((void *, int, int, FILE *));

int    fputs __P((const char *, FILE *));
int    puts __P((const char *));
int    fputc __P((int, FILE *));
int    putw __P((int, FILE *));
int    fwrite __P((const void *, int, int, FILE *));

int    printf __P((const char *, ...));
int    fprintf __P((FILE *, const char *, ...));
char  *sprintf __P((char *, const char *, ...));   /* V7: returns its buffer */
int    scanf __P((const char *, ...));
int    fscanf __P((FILE *, const char *, ...));
int    sscanf __P((const char *, const char *, ...));

void   perror __P((const char *));          /* "msg: error text" on stderr */

/* internal */
int    _filbuf __P((FILE *));
int    _flsbuf __P((unsigned, FILE *));

#endif
