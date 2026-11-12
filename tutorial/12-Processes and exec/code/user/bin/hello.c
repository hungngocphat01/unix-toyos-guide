/* hello.c -- prints its arguments and environment, tests sbrk and exec. */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv, char **envp)
{
    printf("hello: pid %d\n", getpid());
    for (int i = 0; i < argc; i++)
        printf("  argv[%d] = \"%s\"\n", i, argv[i]);
    for (int i = 0; envp[i]; i++)
        printf("  envp[%d] = \"%s\"\n", i, envp[i]);

    /* The heap: 100 KiB from sbrk, then use all of it. */
    char *p = sbrk(100 * 1024);
    memset(p, 'x', 100 * 1024);
    printf("hello: sbrk gave %x, wrote 100 KiB there\n", (unsigned)p);

    char *args[] = { "nope", 0 };
    if (exec("nope", args) < 0)
        printf("hello: exec(\"nope\") failed with errno %d (ENOENT is %d)\n",
               errno, ENOENT);

    return 3;
}
