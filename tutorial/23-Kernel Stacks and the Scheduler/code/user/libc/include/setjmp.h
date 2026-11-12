/* setjmp.h -- jmp_buf holds the callee-saved registers:
 * ebx, esi, edi, ebp, esp, eip. (V7's PDP-11 version had 3.) */
#ifndef _SETJMP_H
#define _SETJMP_H

#include <sys/cdefs.h>

typedef int jmp_buf[6];

int  setjmp __P((jmp_buf));
void longjmp __P((jmp_buf, int)) __attribute__((noreturn));

#endif
