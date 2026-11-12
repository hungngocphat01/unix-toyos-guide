/* echo.c -- print the arguments. (Lesson 22 replaces it with V7's.) */
#include <stdio.h>

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
        printf("%s%s", argv[i], i < argc - 1 ? " " : "");
    printf("\n");
    return 0;
}
