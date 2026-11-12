/* init.c -- the first user program (pid 1). For now: a test driver. */
#include <errno.h>
#include <stdio.h>
#include <unistd.h>

static char *env[] = { "HOME=/", "PATH=:/bin:/usr/bin", 0 };

/* Run a program in a child process and wait for it. */
static void run(char *prog, char *arg)
{
    char *argv[] = { prog, arg, 0 };

    if (fork() == 0) {
        execve(prog, argv, env);
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

    run("/bin/libctest", 0);
    return 0;
}
