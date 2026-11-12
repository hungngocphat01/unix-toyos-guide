/* pipe.h -- a pipe: a buffer in memory, written at one end, read at the other. */
#ifndef FS_PIPE_H
#define FS_PIPE_H

#include <stdint.h>

struct pipe;

#define PIPEDEV 0xFFFF                          /* st_dev of every pipe: no real device */

struct pipe *pipe_alloc(void);                  /* NULL if out of memory */

/* Close one end. The pipe is freed when both ends are closed. */
void pipe_close(struct pipe *p, int writer);

/* buf is a user pointer the caller already checked. */
int pipe_read(struct pipe *p, void *buf, uint32_t n);
int pipe_write(struct pipe *p, const void *buf, uint32_t n);

uint32_t pipe_count(struct pipe *p);            /* bytes waiting to be read */
uint32_t pipe_id(struct pipe *p);               /* a number unique to this pipe */

#endif
