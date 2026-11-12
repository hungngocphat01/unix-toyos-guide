/* tty.c -- the terminal line discipline (V7: tty.c).
 *
 * Characters typed on the keyboard or received on the serial port arrive
 * here (tty_input, in an interrupt handler). In the normal "cooked" mode
 * the TTY collects a whole line, handling erase and kill, and read() only
 * returns once the user presses Enter:
 *
 *   input[]:  [ h e l l o \n | w o r ]
 *               ^r            ^w     ^e
 *   r..w: complete lines, ready for read()
 *   w..e: the line being edited (erase/kill only touch this part)
 *
 * In CBREAK and RAW modes every character is ready immediately (w = e).
 * There is one terminal, the console: minor numbers are ignored.
 */
#include "dev/tty.h"
#include "dev/console.h"
#include "arch/i386/cpu.h"
#include "sys/syscall.h"
#include "abi/sgtty.h"
#include "abi/errno.h"
#include "lib/string.h"

#define TTY_BUF 256                 /* a power of 2: indices wrap with & */
#define CTRL(c) ((c) & 0x1F)

static struct {
    char          input[TTY_BUF];
    uint32_t      r, w, e;          /* free-running indices, see above */
    struct sgttyb mode;
    struct tchars chars;
} tty;

void tty_init(void)
{
    /* V7 used '#' to erase and '@' to kill a line (printing terminals
     * could not erase anything). We use today's keys. */
    tty.mode.sg_erase = '\b';
    tty.mode.sg_kill  = CTRL('U');
    tty.mode.sg_flags = ECHO | CRMOD;
    tty.chars.t_intrc = CTRL('C');
    tty.chars.t_quitc = CTRL('\\');
    tty.chars.t_eofc  = CTRL('D');
    tty.chars.t_startc = CTRL('Q');
    tty.chars.t_stopc  = CTRL('S');
    tty.chars.t_brkc   = -1;
}

static void echo(char c)
{
    if (tty.mode.sg_flags & ECHO)
        console_putc(c);
}

/* Called from the keyboard and serial interrupt handlers. */
void tty_input(char c)
{
    int flags = tty.mode.sg_flags;

    if (flags & RAW) {
        if (tty.e - tty.r < TTY_BUF)
            tty.input[tty.e++ % TTY_BUF] = c;
        tty.w = tty.e;
        return;
    }

    if (c == '\r' && (flags & CRMOD))
        c = '\n';
    if (c == 0x7F)
        c = '\b';                   /* terminals send DEL for Backspace */

    if (!(flags & CBREAK)) {
        if (c == tty.mode.sg_erase) {
            if (tty.e != tty.w) {
                tty.e--;
                echo('\b'); echo(' '); echo('\b');
            }
            return;
        }
        if (c == tty.mode.sg_kill) {
            while (tty.e != tty.w) {
                tty.e--;
                echo('\b'); echo(' '); echo('\b');
            }
            return;
        }
    }
    /* Lesson 26: t_intrc and t_quitc send SIGINT and SIGQUIT. */

    if (tty.e - tty.r >= TTY_BUF - 1 && c != '\n')
        return;                     /* full: drop (keep room for a newline) */
    tty.input[tty.e++ % TTY_BUF] = c;
    if (c != tty.chars.t_eofc)
        echo(c);

    /* A line is complete, or every character counts in CBREAK mode. */
    if ((flags & CBREAK) || c == '\n' || c == tty.chars.t_eofc ||
        tty.e - tty.r == TTY_BUF)
        tty.w = tty.e;
}

/* Wait until there is something to read. We're in a system call with
 * interrupts off: turn them on and halt until an interrupt (maybe a key)
 * arrives. "sti; hlt" can't miss an interrupt between the two
 * instructions: sti takes effect only after the next instruction. */
static void wait_for_input(void)
{
    while (tty.r == tty.w)
        __asm__ volatile ("sti; hlt; cli");
}

int tty_read(int minor, void *buf, uint32_t n)
{
    (void)minor;
    char *dst = buf;
    uint32_t got = 0;

    wait_for_input();
    while (got < n && tty.r != tty.w) {
        char c = tty.input[tty.r++ % TTY_BUF];
        if (tty.mode.sg_flags & (RAW | CBREAK)) {
            dst[got++] = c;
            continue;
        }
        if (c == tty.chars.t_eofc)
            break;                  /* ^D: end the read here; alone = EOF */
        dst[got++] = c;
        if (c == '\n')
            break;                  /* cooked: at most one line per read */
    }
    return got;
}

int tty_write(int minor, const void *buf, uint32_t n)
{
    (void)minor;
    const char *s = buf;
    for (uint32_t i = 0; i < n; i++)
        console_putc(s[i]);         /* serial_putc does NL -> CR NL */
    return n;
}

/* stty/gtty and friends (V7 libc implements stty() as ioctl(TIOCSETP)). */
int tty_ioctl(int minor, uint32_t cmd, void *uarg)
{
    (void)minor;
    switch (cmd) {
    case TIOCGETP:
        if (!user_ok(uarg, sizeof(struct sgttyb), 1))
            return -EFAULT;
        memcpy(uarg, &tty.mode, sizeof(struct sgttyb));
        return 0;
    case TIOCSETP:                  /* set, and throw away unread input */
    case TIOCSETN:                  /* set, keep the input */
        if (!user_ok(uarg, sizeof(struct sgttyb), 0))
            return -EFAULT;
        memcpy(&tty.mode, uarg, sizeof(struct sgttyb));
        if (cmd == TIOCSETP)
            tty.r = tty.w = tty.e;
        else if (tty.mode.sg_flags & (RAW | CBREAK))
            tty.w = tty.e;          /* a half-typed line becomes readable */
        return 0;
    case TIOCGETC:
        if (!user_ok(uarg, sizeof(struct tchars), 1))
            return -EFAULT;
        memcpy(uarg, &tty.chars, sizeof(struct tchars));
        return 0;
    case TIOCSETC:
        if (!user_ok(uarg, sizeof(struct tchars), 0))
            return -EFAULT;
        memcpy(&tty.chars, uarg, sizeof(struct tchars));
        return 0;
    }
    return -ENOTTY;
}
