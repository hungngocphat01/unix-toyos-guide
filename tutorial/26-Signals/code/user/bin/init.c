/* init.c -- process 1: the ancestor of every other process.
 *
 * Like V7's init, simplified:
 *   1. open the console as fd 0, 1 and 2; every process inherits them
 *   2. run the startup script /etc/rc
 *   3. run a shell on the console; when it exits, start a new one
 * and, all along, wait() for orphans (processes whose parent died are
 * re-parented to init, which must collect their exit status).
 *
 * V7 then went multi-user: a getty + login per terminal (/etc/ttys).
 */
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>

static char *env[] = { "HOME=/", "PATH=:/bin:/usr/bin", 0 };

/* Start a program and wait until it ends. */
static void spawn(char **argv)
{
    int pid = fork();
    if (pid == 0) {
        execve(argv[0], argv, env);
        fprintf(stderr, "init: cannot run %s\n", argv[0]);
        _exit(1);
    }
    while (wait((int *)0) != pid)
        ;                       /* orphans exiting meanwhile are collected too */
}

int main(void)
{
    open("/dev/console", 2);
    dup(0);
    dup(0);

    struct stat st;
    char *rc[] = { "/bin/sh", "/etc/rc", 0 };
    if (stat("/etc/rc", &st) == 0)
        spawn(rc);

    for (;;) {
        char *sh[] = { "/bin/sh", 0 };
        spawn(sh);
        printf("init: shell exited, starting another one\n");
    }
}
