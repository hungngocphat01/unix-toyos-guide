/* cat.c -- copy files (or stdin) to stdout. */
#include <stdio.h>
#include <unistd.h>

static void copy(int fd)
{
    char buf[512];
    int n;
    while ((n = read(fd, buf, sizeof(buf))) > 0)
        write(1, buf, n);
}

int main(int argc, char **argv)
{
    if (argc == 1)
        copy(0);
    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], 0);
        if (fd < 0) {
            printf("cat: can't open %s\n", argv[i]);
            continue;
        }
        copy(fd);
        close(fd);
    }
    return 0;
}
