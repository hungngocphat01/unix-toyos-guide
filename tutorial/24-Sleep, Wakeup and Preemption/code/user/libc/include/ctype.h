/* ctype.h -- V7's character classes: one table lookup per test.
 * (_ctype_ + 1) so that EOF (-1) is a valid index. */
#ifndef _CTYPE_H
#define _CTYPE_H

#define _U  01      /* upper case */
#define _L  02      /* lower case */
#define _N  04      /* digit */
#define _S  010     /* space */
#define _P  020     /* punctuation */
#define _C  040     /* control */
#define _X  0100    /* hex digit */

extern char _ctype_[];

#define isalpha(c)  ((_ctype_+1)[c] & (_U|_L))
#define isupper(c)  ((_ctype_+1)[c] & _U)
#define islower(c)  ((_ctype_+1)[c] & _L)
#define isdigit(c)  ((_ctype_+1)[c] & _N)
#define isxdigit(c) ((_ctype_+1)[c] & (_N|_X))
#define isspace(c)  ((_ctype_+1)[c] & _S)
#define ispunct(c)  ((_ctype_+1)[c] & _P)
#define isalnum(c)  ((_ctype_+1)[c] & (_U|_L|_N))
#define isprint(c)  ((_ctype_+1)[c] & (_P|_U|_L|_N))
#define iscntrl(c)  ((_ctype_+1)[c] & _C)
#define isascii(c)  ((unsigned)(c) <= 0177)
#define toupper(c)  ((c) - 'a' + 'A')       /* V7: only for lower case letters! */
#define tolower(c)  ((c) - 'A' + 'a')
#define toascii(c)  ((c) & 0177)

#endif
