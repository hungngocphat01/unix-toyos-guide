/* exit.c -- flush stdio's buffers, then really exit. (V7: cuexit.s) */
#include <unistd.h>

void _cleanup();                /* v7/flsbuf.c: fclose every open FILE */

void exit(int status)
{
    _cleanup();
    _exit(status);
}
