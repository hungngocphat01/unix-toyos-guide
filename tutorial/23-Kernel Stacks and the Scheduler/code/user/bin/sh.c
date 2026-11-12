/* sh.c -- a small shell.
 *
 *   ls -l /bin                 run a program (found through $PATH)
 *   cat < in > out             redirect stdin / stdout
 *   echo more >> out           append
 *   cd /tmp; ls                several commands on a line
 *   cc big.c &                 run in the background (lesson 23)
 *   echo 'a  b' "c ; d" e\;f   quoting
 *   cd [dir], exit [n], wait   built in
 *   sh script, sh -c 'cmds'    read commands from a file or a string
 *
 * Lesson 25 adds pipes (|); lesson 26 adds signals (^C).
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "abi/signal.h"

#define LINE_MAX 512
#define MAXARGS  64

/* One simple command: its words and its redirections. */
struct cmd {
    char *argv[MAXARGS + 1];
    char *in;                   /* < file, or NULL  */
    char *out;                  /* > file, or NULL  */
    int   append;               /* 1 if >> */
};

enum token { END, WORD, SEMI, AMP, IN, OUT, APPEND, BAD };

/* The tokenizer reads from `in` and copies words (without their quotes)
 * into `words`, one after the other, each NUL-terminated. */
struct lexer {
    char *in;
    char *words;
    char *word;                 /* the last WORD token */
};

static int is_special(char c)
{
    return c == '\0' || c == '\n' || c == ' ' || c == '\t' ||
           c == ';' || c == '&' || c == '<' || c == '>';
}

/* Copy one word, removing quotes: 'single' quotes everything,
 * "double" too (no $ yet), and \ quotes the next character.
 * Returns 0 on an unterminated quote. */
static int read_word(struct lexer *lx)
{
    char *s = lx->in, *d = lx->words;

    lx->word = d;
    while (!is_special(*s)) {
        if (*s == '\'' || *s == '"') {
            char quote = *s++;
            while (*s && *s != quote)
                *d++ = *s++;
            if (*s++ != quote)
                return 0;
        } else if (*s == '\\' && s[1]) {
            s++;
            *d++ = *s++;
        } else {
            *d++ = *s++;
        }
    }
    *d++ = '\0';
    lx->in = s;
    lx->words = d;
    return 1;
}

static enum token next(struct lexer *lx)
{
    while (*lx->in == ' ' || *lx->in == '\t')
        lx->in++;

    switch (*lx->in) {
    case '\0':
    case '\n':
        return END;
    case ';':
        lx->in++;
        return SEMI;
    case '&':
        lx->in++;
        return AMP;
    case '<':
        lx->in++;
        return IN;
    case '>':
        if (*++lx->in == '>') {
            lx->in++;
            return APPEND;
        }
        return OUT;
    }
    return read_word(lx) ? WORD : BAD;
}

/* Parse one command (up to ';', '&' or the end of the line) into c.
 * Returns the token that ended it (SEMI, AMP or END), or BAD on a syntax error. */
static enum token parse(struct lexer *lx, struct cmd *c)
{
    int argc = 0;
    memset(c, 0, sizeof(*c));

    for (;;) {
        enum token t = next(lx);
        switch (t) {
        case WORD:
            if (argc == MAXARGS) {
                fprintf(stderr, "sh: too many arguments\n");
                return BAD;
            }
            c->argv[argc++] = lx->word;
            break;
        case IN:
        case OUT:
        case APPEND:
            if (next(lx) != WORD) {
                fprintf(stderr, "sh: syntax error: file name expected\n");
                return BAD;
            }
            if (t == IN)
                c->in = lx->word;
            else {
                c->out = lx->word;
                c->append = t == APPEND;
            }
            break;
        case BAD:
            fprintf(stderr, "sh: syntax error: unterminated quote\n");
            return BAD;
        default:                /* SEMI, AMP or END */
            c->argv[argc] = NULL;
            return t;
        }
    }
}

/* ---- running ---- */

/* Make `file` the new file descriptor fd (0 or 1). V7 has no dup2: close
 * fd, and dup() returns the lowest free descriptor, which is now fd. */
static int redirect(int fd, const char *file, int append)
{
    int f;

    if (fd == 0)
        f = open(file, 0);
    else if (append && (f = open(file, 1)) >= 0)
        lseek(f, 0L, 2);        /* V7 has no O_APPEND: seek to the end */
    else
        f = creat(file, 0666);

    if (f < 0) {
        fprintf(stderr, "sh: cannot %s %s\n", fd == 0 ? "open" : "create", file);
        return -1;
    }
    close(fd);
    dup(f);
    close(f);
    return 0;
}

/* V7 sh's messages for processes killed by a signal (index: signal number). */
static const char *signal_msg[NSIG] = {
    [SIGHUP] = "Hangup",          [SIGQUIT] = "Quit",
    [SIGILL] = "Illegal instruction", [SIGTRAP] = "Trace/BPT trap",
    [SIGIOT] = "IOT trap",        [SIGEMT] = "EMT trap",
    [SIGFPE] = "Floating exception", [SIGKILL] = "Killed",
    [SIGBUS] = "Bus error",       [SIGSEGV] = "Memory fault",
    [SIGSYS] = "Bad system call", [SIGALRM] = "Alarm call",
    [SIGTERM] = "Terminated",
};

static void report(int status)
{
    int sig = status & 0177;
    if (sig && sig < NSIG && signal_msg[sig])
        fprintf(stderr, "%s%s\n", signal_msg[sig], status & 0200 ? " -- core dumped" : "");
}

static int last_status;         /* exit status of the last command */

/* Run a command. In the background (&): don't wait for it. */
static void run(struct cmd *c, int background)
{
    char **argv = c->argv;

    if (!argv[0])
        return;                 /* empty command */

    /* Built-ins: they must change the shell itself, not a child. */
    if (strcmp(argv[0], "cd") == 0) {
        char *dir = argv[1] ? argv[1] : getenv("HOME");
        if (!dir || chdir(dir) < 0)
            fprintf(stderr, "sh: %s: bad directory\n", dir ? dir : "(no HOME)");
        return;
    }
    if (strcmp(argv[0], "exit") == 0)
        exit(argv[1] ? atoi(argv[1]) : last_status);
    if (strcmp(argv[0], "wait") == 0) {     /* wait for all background jobs */
        int status;
        while (wait(&status) != -1)
            report(status);
        return;
    }

    int pid = fork();
    if (pid < 0) {
        fprintf(stderr, "sh: cannot fork\n");
        return;
    }
    if (pid == 0) {             /* child: set up its files, become the program */
        if (c->in && redirect(0, c->in, 0) < 0)
            _exit(1);
        if (c->out && redirect(1, c->out, c->append) < 0)
            _exit(1);
        execvp(argv[0], argv);
        fprintf(stderr, "%s: %s\n", argv[0], errno == ENOENT ? "not found" : "cannot execute");
        _exit(127);             /* not exit(): don't flush the parent's stdio buffers */
    }

    if (background) {
        fprintf(stderr, "%d\n", pid);      /* V7 sh prints the job's pid */
        return;
    }
    int status, w;
    while ((w = wait(&status)) != pid && w != -1)
        ;                       /* background jobs that ended meanwhile */
    report(status);
    last_status = (status >> 8) & 0377;
}

/* Run every command on a line. */
static void run_line(char *line)
{
    static char words[LINE_MAX];
    struct lexer lx = { line, words, NULL };
    struct cmd c;
    enum token t;

    do {
        t = parse(&lx, &c);
        if (t == BAD)
            return;
        run(&c, t == AMP);
    } while (t == SEMI || t == AMP);
}

int main(int argc, char **argv)
{
    char line[LINE_MAX];
    FILE *in = stdin;

    if (argc > 2 && strcmp(argv[1], "-c") == 0) {
        run_line(argv[2]);
        return last_status;
    }
    if (argc > 1 && !(in = fopen(argv[1], "r"))) {
        fprintf(stderr, "sh: cannot open %s\n", argv[1]);
        return 2;
    }
    int interactive = in == stdin && isatty(0);

    for (;;) {
        if (interactive)
            fputs("$ ", stderr);
        if (!fgets(line, sizeof(line), in))
            break;              /* end of file: ^D on a terminal */
        run_line(line);
    }
    return last_status;
}
