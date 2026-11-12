/* ls.c -- list a directory the V7 way: read() it as struct direct entries. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/dir.h>

static void list(const char *dir)
{
    int fd = open(dir, 0);
    if (fd < 0) {
        printf("ls: can't open %s\n", dir);
        return;
    }
    printf("%s:\n", dir);

    struct direct d;
    while (read(fd, &d, sizeof(d)) == sizeof(d)) {
        if (d.d_ino == 0)
            continue;
        char name[DIRSIZ + 1];
        memcpy(name, d.d_name, DIRSIZ);
        name[DIRSIZ] = '\0';

        char path[256];
        strcpy(path, dir);
        strcat(path, "/");
        strcat(path, name);

        struct stat st;
        if (stat(path, &st) < 0)
            printf("%5u  ??????  %s\n", d.d_ino, name);
        else
            printf("%5u  %06o %6d  %s\n", d.d_ino, st.st_mode, (int)st.st_size, name);
    }
    close(fd);
}

int main(int argc, char **argv)
{
    if (argc == 1)
        list(".");
    for (int i = 1; i < argc; i++)
        list(argv[i]);
    return 0;
}
