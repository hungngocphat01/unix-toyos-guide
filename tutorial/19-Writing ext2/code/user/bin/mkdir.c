/* mkdir.c -- make directories, the V7 way: there's no mkdir system call.
 * mknod makes an empty directory inode, then two links fill it in:
 *
 *   mknod("d", 040777)    d           (no entries yet)
 *   link("d", "d/.")      d/.  -> d
 *   link(".", "d/..")     d/.. -> parent
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int make(char *d)
{
    char parent[128], dname[128];
    int slash = 0;

    for (int i = 0; d[i]; i++)          /* parent = everything up to the last '/' */
        if (d[i] == '/')
            slash = i + 1;
    memcpy(parent, d, slash);
    strcpy(parent + slash, ".");

    if (mknod(d, 040777, 0) < 0) {
        printf("mkdir: cannot make directory %s\n", d);
        return 1;
    }
    strcpy(dname, d);
    strcat(dname, "/.");
    if (link(d, dname) < 0) {
        printf("mkdir: cannot link %s\n", dname);
        unlink(d);
        return 1;
    }
    strcat(dname, ".");
    if (link(parent, dname) < 0) {
        printf("mkdir: cannot link %s\n", dname);
        dname[strlen(dname) - 1] = '\0';
        unlink(dname);
        unlink(d);
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    int errors = 0;
    if (argc < 2)
        printf("usage: mkdir dir...\n");
    for (int i = 1; i < argc; i++)
        errors += make(argv[i]);
    return errors != 0;
}
