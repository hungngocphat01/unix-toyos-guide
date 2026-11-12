/* init.c -- the first user program (pid 1). For now: a test driver. */
#include <errno.h>
#include <stdio.h>
#include <unistd.h>

/* Run a program in a child process and wait for it. */
static void run(char *prog, char *arg)
{
    char *argv[] = { prog, arg, 0 };

    if (fork() == 0) {
        exec(prog, argv);
        printf("init: cannot exec %s, errno %d\n", prog, errno);
        _exit(127);
    }
    int status;
    wait(&status);
    if (status != 0)
        printf("init: %s ended with status %x\n", prog, status);
}

int main(void)
{
    open("/dev/console", 2);
    dup(0);
    dup(0);

    printf("init: isatty(0) = %d, isatty of a file = %d\n",
           isatty(0), isatty(open("/etc/motd", 0)));

    printf("\ninit: running cat: type lines, end with ^D\n");
    run("/bin/cat", 0);

    printf("\ninit: running keys\n");
    run("/bin/keys", 0);

    printf("init: done\n");
    return 0;
}
