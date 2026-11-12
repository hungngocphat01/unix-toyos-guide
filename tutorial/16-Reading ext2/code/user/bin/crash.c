/* crash.c -- dereferences NULL. The kernel should kill it, not panic. */
int main(void)
{
    volatile int *p = 0;
    return *p;
}
