/* keys.c -- show the code of each key as it is pressed (CBREAK, no echo).
 * Press q to quit. */
#include <sgtty.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    struct sgttyb old, mode;

    if (!isatty(0)) {
        printf("keys: stdin is not a terminal\n");
        return 1;
    }
    gtty(0, &old);
    mode = old;
    mode.sg_flags |= CBREAK;
    mode.sg_flags &= ~ECHO;
    stty(0, &mode);

    printf("keys: press keys, q to quit\n");
    char c;
    while (read(0, &c, 1) == 1 && c != 'q')
        printf("  key %d (0%o)\n", c, c);

    stty(0, &old);                  /* always restore the terminal! */
    return 0;
}
