/* libctest.c -- try the V7 C library. */
#include <ctype.h>
#include <errno.h>
#include <pwd.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int compare(const void *a, const void *b)
{
    return strcmp(*(char **)a, *(char **)b);
}

static jmp_buf again;

static void deep(int n)
{
    if (n > 0)
        deep(n - 1);
    else if (n == 0)
        longjmp(again, 42);         /* straight back into setjmp */
}

int main(void)
{
    char buf[100];

    printf("== printf\n");
    printf("[%5d] [%-5d] [%05d] [%d]\n", 42, 42, 42, -7);
    printf("[%x] [%o] [%u] [%c] [%s] [%.3s] [%8s]\n", 255, 8, 3000000000u, 'A', "str", "truncated", "right");
    printf("[%f] [%.2f] [%e] [%g] [%g]\n", 3.14159, 2.0 / 3, 12345.678, 0.0001, 1e20);
    printf("sprintf returns its buffer: %s\n", sprintf(buf, "%d-%s", 7, "up"));

    printf("== files\n");
    FILE *f = fopen("/tmp/numbers", "w");
    for (int i = 1; i <= 5; i++)
        fprintf(f, "%d squared is %d\n", i, i * i);
    fclose(f);
    f = fopen("/tmp/numbers", "r");
    int a, b, sum = 0;
    while (fscanf(f, "%d squared is %d", &a, &b) == 2)
        sum += b;
    fclose(f);
    printf("sum of squares read back with fscanf: %d\n", sum);
    f = fopen("/tmp/numbers", "r");
    fgets(buf, sizeof(buf), f);
    printf("first line with fgets: %s", buf);
    fclose(f);
    if (!fopen("/nonexistent", "r"))
        perror("fopen /nonexistent");

    printf("== malloc, qsort\n");
    char *words[] = { "pear", "apple", "fig", "banana", "cherry" };
    qsort((char *)words, 5, sizeof(char *), (int (*)())compare);
    for (int i = 0; i < 5; i++)
        printf("%s ", words[i]);
    printf("\n");
    char *blocks[50];
    for (int i = 0; i < 50; i++)
        blocks[i] = malloc(100 + i * 37);
    for (int i = 0; i < 50; i += 2)
        free(blocks[i]);
    char *big = malloc(20000);
    printf("malloc: 50 blocks and a 20000-byte one at %x (sbrk(0) = %x)\n",
           (unsigned)big, (unsigned)sbrk(0));

    printf("== strings and numbers\n");
    printf("atoi(\"  -123\") = %d, atof(\"2.5e3\") = %g\n", atoi("  -123"), atof("2.5e3"));
    int x;
    char word[20];
    sscanf("hex ff", "%s %x", word, &x);
    printf("sscanf(\"hex ff\", \"%%s %%x\") -> \"%s\" %d\n", word, x);
    printf("isdigit('7') %d, isalpha('7') %d, toupper('q') %c\n",
           isdigit('7') != 0, isalpha('7') != 0, toupper('q'));

    printf("== time, users, environment\n");
    time_t now = time(0);
    printf("ctime: %s", ctime(&now));
    struct passwd *pw = getpwuid(0);
    printf("getpwuid(0): %s, home %s, shell %s\n", pw->pw_name, pw->pw_dir, pw->pw_shell);
    printf("getenv(\"PATH\") = %s\n", getenv("PATH"));
    printf("ttyname(0) = %s\n", ttyname(0));

    printf("== setjmp\n");
    int r = setjmp(again);
    if (r == 0)
        deep(10);
    printf("setjmp returned %d after longjmp\n", r);

    return 0;                       /* exit() flushes stdio buffers */
}
