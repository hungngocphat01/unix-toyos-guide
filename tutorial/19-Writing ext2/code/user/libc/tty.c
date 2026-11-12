/* tty.c -- V7 implements these with ioctl, not with system calls. */
#include <sgtty.h>
#include <unistd.h>

int stty(int fd, struct sgttyb *mode)
{
    return ioctl(fd, TIOCSETP, mode);
}

int gtty(int fd, struct sgttyb *mode)
{
    return ioctl(fd, TIOCGETP, mode);
}

/* A file is a terminal if it has terminal modes. */
int isatty(int fd)
{
    struct sgttyb mode;
    return gtty(fd, &mode) == 0;
}
