/* pipe.c -- pipes (V7: pipe.c).
 *
 * A pipe is a ring buffer of PIPESIZE bytes:
 *
 *   data: [ . . . r e a d y . . ]      r: next byte to read
 *                 ^r        ^w         w: next byte to write
 *
 * r and w only grow, and wrap with % PIPESIZE: w - r is the number of
 * bytes in the pipe. A reader sleeps while it's empty, a writer while it's
 * full; each wakes the other.
 *
 * When every writer has closed its end, read returns 0 (end of file).
 * When every reader has closed, write fails with EPIPE: nobody will ever
 * read the data, and the writer gets SIGPIPE (sys_write).
 */
#include "fs/pipe.h"
#include "proc/proc.h"
#include "mm/kheap.h"
#include "mm/pmm.h"
#include "mm/memlayout.h"
#include "abi/errno.h"

#define PIPESIZE PAGE_SIZE          /* V7: PIPSIZ 4096 */

struct pipe {
    char     *data;                 /* one page */
    uint32_t  r, w;                 /* free-running, see above */
    int       reader_open;          /* is the read end still open? */
    int       writer_open;          /* is the write end? */
    uint32_t  id;                   /* reported as its inode number by fstat */
};

static uint32_t next_id = 1;

struct pipe *pipe_alloc(void)
{
    struct pipe *p = kmalloc(sizeof(*p));
    if (!p)
        return NULL;
    uint32_t page = pmm_alloc();
    if (!page) {
        kfree(p);
        return NULL;
    }
    p->data = P2V(page);
    p->reader_open = p->writer_open = 1;
    p->id = next_id++;
    return p;
}

void pipe_close(struct pipe *p, int writer)
{
    if (writer) {
        p->writer_open = 0;
        wakeup(&p->r);              /* readers: end of file */
    } else {
        p->reader_open = 0;
        wakeup(&p->w);              /* writers: nobody will read */
    }
    if (!p->reader_open && !p->writer_open) {
        pmm_free(V2P(p->data));
        kfree(p);
    }
}

int pipe_read(struct pipe *p, void *buf, uint32_t n)
{
    char *dst = buf;

    while (p->r == p->w) {          /* empty */
        if (!p->writer_open)
            return 0;               /* and will stay empty: end of file */
        if (sleep(&p->r) < 0)
            return -EINTR;
    }
    uint32_t got = 0;               /* take what's there, don't wait for n */
    while (got < n && p->r != p->w)
        dst[got++] = p->data[p->r++ % PIPESIZE];
    wakeup(&p->w);                  /* there's room now */
    return got;
}

int pipe_write(struct pipe *p, const void *buf, uint32_t n)
{
    const char *src = buf;

    for (uint32_t put = 0; put < n; put++) {
        while (p->w - p->r == PIPESIZE) {   /* full */
            if (!p->reader_open)
                return -EPIPE;
            wakeup(&p->r);          /* let the reader empty it */
            if (sleep(&p->w) < 0)
                return put ? (int)put : -EINTR;     /* report what got through */
        }
        if (!p->reader_open)
            return -EPIPE;
        p->data[p->w++ % PIPESIZE] = src[put];
    }
    wakeup(&p->r);
    return n;
}

uint32_t pipe_count(struct pipe *p)
{
    return p->w - p->r;
}

uint32_t pipe_id(struct pipe *p)
{
    return p->id;
}
