/* init.c -- the first user program (pid 1). For now: a test driver. */
#include <errno.h>
#include <stdio.h>
#include <unistd.h>

/* Run a program in a child process and report how it ended. */
static void run(char *prog, char *arg)
{
    char *argv[] = { prog, arg, 0 };

    int pid = fork();
    if (pid < 0) {
        printf("init: fork failed, errno %d\n", errno);
        return;
    }
    if (pid == 0) {                         /* child */
        exec(prog, argv);
        printf("init: cannot exec %s, errno %d\n", prog, errno);
        _exit(127);
    }

    int status;                             /* parent */
    int done = wait(&status);
    if (status & 0xFF)
        printf("init: pid %d (%s) was killed by signal %d\n", done, prog, status & 0xFF);
    else
        printf("init: pid %d (%s) exited with code %d\n", done, prog, status >> 8);
}

int counter = 1;

int main(void)
{
    printf("init: I am pid %d\n", getpid());

    /* 1. fork without exec: the child gets a COPY of our memory. */
    int pid = fork();
    if (pid == 0) {
        counter = 100;
        printf("child: fork returned 0, my pid is %d, counter = %d\n", getpid(), counter);
        _exit(5);
    }
    int status;
    wait(&status);
    printf("init: fork returned %d; my counter is still %d; child exit code %d\n",
           pid, counter, status >> 8);

    /* 2. fork + exec + wait: what a shell does. */
    run("hello", "world");
    run("crash", 0);
    run("missing", 0);

    /* 3. wait with no children left. */
    if (wait(&status) < 0)
        printf("init: wait with no children: errno %d (ECHILD is %d)\n", errno, ECHILD);
    return 0;
}
