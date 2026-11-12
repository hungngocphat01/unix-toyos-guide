/* fstest.c -- exercise the writable filesystem. */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static int failures;

static void check(int ok, const char *what)
{
    printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        failures++;
}

static int file_size(const char *path)
{
    struct stat st;
    return stat(path, &st) < 0 ? -1 : (int)st.st_size;
}

static int nlink(const char *path)
{
    struct stat st;
    return stat(path, &st) < 0 ? -1 : st.st_nlink;
}

/* Read a whole small file into buf (NUL-terminated). */
static int slurp(const char *path, char *buf, int max)
{
    int fd = open(path, 0);
    if (fd < 0)
        return -1;
    int n = read(fd, buf, max - 1);
    close(fd);
    buf[n > 0 ? n : 0] = '\0';
    return n;
}

static void test_create(void)
{
    char buf[64];
    printf("creat, write, read:\n");
    int fd = creat("/tmp/hello", 0644);
    check(fd >= 0, "creat /tmp/hello");
    check(write(fd, "hello, disk\n", 12) == 12, "write 12 bytes");
    close(fd);
    check(slurp("/tmp/hello", buf, sizeof(buf)) == 12 && strcmp(buf, "hello, disk\n") == 0,
          "read them back");

    fd = creat("/tmp/hello", 0644);
    close(fd);
    check(file_size("/tmp/hello") == 0, "creat on an existing file empties it");
    fd = open("/tmp/hello", 1);
    write(fd, "again\n", 6);
    close(fd);
}

static void test_links(void)
{
    char buf[64];
    printf("link, unlink:\n");
    check(link("/tmp/hello", "/tmp/hi") == 0, "link /tmp/hello /tmp/hi");
    check(nlink("/tmp/hi") == 2, "2 links");
    check(link("/tmp/hello", "/tmp/hi") < 0 && errno == EEXIST, "link again: EEXIST");
    check(unlink("/tmp/hello") == 0, "unlink /tmp/hello");
    check(open("/tmp/hello", 0) < 0 && errno == ENOENT, "/tmp/hello is gone");
    check(slurp("/tmp/hi", buf, sizeof(buf)) == 6, "/tmp/hi still has the data");
    check(nlink("/tmp/hi") == 1, "1 link");

    int fd = open("/tmp/hi", 0);
    unlink("/tmp/hi");
    check(read(fd, buf, sizeof(buf)) == 6, "an unlinked file stays readable while open");
    close(fd);                      /* now it is really deleted */
}

static void test_big(void)
{
    static char buf[1024];
    printf("a 300 KiB file (single and double indirect blocks):\n");

    int fd = creat("/tmp/big", 0644);
    for (int i = 0; i < 300; i++) {        /* block i is full of letter i */
        memset(buf, 'A' + i % 26, sizeof(buf));
        write(fd, buf, sizeof(buf));
    }
    close(fd);
    check(file_size("/tmp/big") == 300 * 1024, "size is 307200");

    int good = 1;
    fd = open("/tmp/big", 0);
    for (int i = 0; i < 300 && good; i++) {
        lseek(fd, i * 1024L + 500, 0);
        good = read(fd, buf, 1) == 1 && buf[0] == 'A' + i % 26;
    }
    close(fd);
    check(good, "every block reads back");
    check(unlink("/tmp/big") == 0, "unlink it (frees ~303 blocks)");
}

static void test_mkdir(void)
{
    printf("directories:\n");
    if (fork() == 0) {
        char *argv[] = { "mkdir", "/tmp/d", 0 };
        exec("/bin/mkdir", argv);
        _exit(1);
    }
    int status;
    wait(&status);
    check(status == 0, "mkdir /tmp/d");
    check(nlink("/tmp/d") == 2, "/tmp/d has 2 links (its entry and its \".\")");
    int fd = creat("/tmp/d/file", 0600);
    close(fd);
    check(chdir("/tmp/d") == 0 && open("file", 0) >= 0, "chdir /tmp/d; open file");
    check(open("../d/./file", 0) >= 0, "open ../d/./file");
    check(mknod("/tmp/d", 040777, 0) < 0 && errno == EEXIST, "mknod again: EEXIST");
}

/* /tmp/boots counts the boots: proof that changes survive a reboot. */
static void count_boots(void)
{
    char buf[16];
    int boots = 0;
    if (slurp("/tmp/boots", buf, sizeof(buf)) > 0)
        for (char *p = buf; *p >= '0' && *p <= '9'; p++)
            boots = boots * 10 + *p - '0';
    boots++;

    int fd = creat("/tmp/boots", 0644);
    int n = 0;
    char digits[16];
    for (int b = boots; b; b /= 10)
        digits[n++] = '0' + b % 10;
    while (n)
        write(fd, &digits[--n], 1);
    close(fd);
    printf("boot number %d\n", boots);
}

int main(void)
{
    long t = time(0);
    printf("time: %u seconds since 1970 (day %u)\n", (unsigned)t, (unsigned)(t / 86400));
    count_boots();
    if (nlink("/tmp/d") > 0) {
        printf("(already tested on a previous boot)\n");
        return 0;
    }
    test_create();
    test_links();
    test_big();
    test_mkdir();
    printf("%d failure(s)\n", failures);
    return failures != 0;
}
