/* init.c -- the first user program (pid 1). For now: a test driver. */
#include <errno.h>
#include <stdio.h>
#include <unistd.h>

/* Run a program in a child process and wait for it. */
static void run(char *prog, char *arg)
{
    char *argv[] = { prog, arg, 0 };
    printf("\ninit: running %s %s\n", prog, arg ? arg : "");

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
    /* Like V7's init: fd 0, 1 and 2 are the console. */
    open("/dev/console", 2);
    dup(0);
    dup(0);
    printf("init: console open as fd 0, 1, 2\n");

    run("/bin/cat", "/etc/motd");
    run("/bin/ls", "/");
    run("/bin/ls", "/dev");

    /* Relative paths use the current directory, inherited by children. */
    chdir("/etc");
    run("/bin/cat", "motd");

    /* lseek: skip the first 11 bytes ("Welcome to ") */
    char buf[32];
    int fd = open("motd", 0);
    lseek(fd, 11, 0);
    int n = read(fd, buf, sizeof(buf));
    printf("\ninit: after lseek(fd, 11, 0): \"");
    write(1, buf, n);
    close(fd);

    fd = open("/etc/motd", 1);
    if (write(fd, "x", 1) < 0)
        printf("init: write to a file: errno %d (EROFS = %d, until lesson 19)\n", errno, EROFS);
    close(fd);
    run("/bin/crash", 0);
    return 0;
}
