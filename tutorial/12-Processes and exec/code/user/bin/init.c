/* init.c -- the first user program (pid 1). */
#include <errno.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    printf("init: I am pid %d, argc=%d, argv[0]=%s\n", getpid(), argc, argv[0]);

    char *args[] = { "hello", "one", "two", 0 };
    char *env[] = { "HOME=/", "USER=root", 0 };
    exece("hello", args, env);

    printf("init: exec failed, errno=%d\n", errno);
    return 1;
}
