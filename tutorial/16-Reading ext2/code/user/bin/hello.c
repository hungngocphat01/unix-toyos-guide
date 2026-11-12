/* hello.c -- prints its arguments. */
#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    printf("hello: pid %d,", getpid());
    for (int i = 0; i < argc; i++)
        printf(" argv[%d]=\"%s\"", i, argv[i]);
    printf("\n");
    return 3;
}
