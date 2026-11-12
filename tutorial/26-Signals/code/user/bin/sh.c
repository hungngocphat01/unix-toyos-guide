/* sh.c -- a small shell.
 *
 *   ls -l /bin                 run a program (found through $PATH)
 *   cat < in > out             redirect stdin / stdout
 *   echo more >> out           append
 *   ls /bin | wc -l            pipelines (lesson 25)
 *   cd /tmp; ls                several commands on a line
 *   cc big.c &                 run in the background (lesson 23)
 *   echo 'a  b' "c ; d" e\;f   quoting
 *   cd [dir], exit [n], wait   built in
 *   sh script, sh -c 'cmds'    read commands from a file or a string
 *
 * Signals, as V7's sh handles them: ^C at the prompt discards the line;
 * ^C and ^\ reach the foreground command, but not background ones.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#define LINE_MAX 512
#define MAXARGS  64
#define MAXPIPE  8              /* commands in one pipeline */

/* One simple command: its words and its redirections. */
struct cmd {
    char *argv[MAXARGS + 1];
    char *in;                   /* < file, or NULL  */
    char *out;                  /* > file, or NULL  */
    int   append;               /* 1 if >> */
};

enum token { END, WORD, SEMI, AMP, PIPE, IN, OUT, APPEND, BAD };

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
           c == ';' || c == '&' || c == '|' || c == '<' || c == '>';
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
    case '|':
        lx->in++;
        return PIPE;
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

/* Parse one command (up to '|', ';', '&' or the end of the line) into c.
 * Returns the token that ended it, or BAD on a syntax error. */
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
        default:                /* PIPE, SEMI, AMP or END */
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
    if (sig == SIGINT)
        fputc('\n', stderr);   /* the ^C wasn't echoed: end the line */
    if (sig && sig < NSIG && signal_msg[sig])
        fprintf(stderr, "%s%s\n", signal_msg[sig], status & 0200 ? " -- core dumped" : "");
}

static int last_status;         /* exit status of the last command */
static int interactive;         /* reading commands from a terminal */
static volatile int interrupted;/* ^C arrived (see on_interrupt) */

/* wait() that isn't stopped by ^C: the shell catches SIGINT, so a ^C for
 * the foreground command also interrupts the shell's wait() for it. */
static int wait_child(int *status)
{
    int w;
    while ((w = wait(status)) == -1 && errno == EINTR)
        ;
    return w;
}

/* Commands that must change the shell itself, not a child.
 * Returns 1 if c was one of them. */
static int builtin(struct cmd *c)
{
    char **argv = c->argv;

    if (strcmp(argv[0], "cd") == 0) {
        char *dir = argv[1] ? argv[1] : getenv("HOME");
        if (!dir || chdir(dir) < 0)
            fprintf(stderr, "sh: %s: bad directory\n", dir ? dir : "(no HOME)");
        return 1;
    }
    if (strcmp(argv[0], "exit") == 0)
        exit(argv[1] ? atoi(argv[1]) : last_status);
    if (strcmp(argv[0], "wait") == 0) {     /* wait for all background jobs */
        int status;
        while (wait_child(&status) != -1)
            report(status);
        return 1;
    }
    return 0;
}

/* Make fd (0 or 1) a copy of `from`, then close `from`. */
static void move_fd(int from, int fd)
{
    close(fd);
    dup(from);                  /* lowest free fd: fd */
    close(from);
}

/* Start one command in a child process, with stdin from `in` and stdout to
 * `out` (pipe ends; -1 = keep the shell's). Returns the child's pid. */
static int start(struct cmd *c, int in, int out, int unused, int background)
{
    int pid = fork();
    if (pid != 0)
        return pid;             /* the shell, or -1 */

    /* The child. Its signals: exec keeps "ignored" and resets "caught"
     * to the default. Undo what the interactive shell changed for itself;
     * then a background job ignores ^C and ^\, which go to every process
     * on the terminal. */
    if (interactive) {
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGTERM, SIG_DFL);
    }
    if (background) {
        signal(SIGINT, SIG_IGN);
        signal(SIGQUIT, SIG_IGN);
    }

    /* Set up its files, become the program. */
    if (in >= 0)
        move_fd(in, 0);
    if (out >= 0)
        move_fd(out, 1);
    if (unused >= 0)
        close(unused);          /* the next command's end of our output pipe */
    if (c->in && redirect(0, c->in, 0) < 0)
        _exit(1);
    if (c->out && redirect(1, c->out, c->append) < 0)
        _exit(1);
    execvp(c->argv[0], c->argv);
    fprintf(stderr, "%s: %s\n", c->argv[0], errno == ENOENT ? "not found" : "cannot execute");
    _exit(127);                 /* not exit(): don't flush the parent's stdio buffers */
}

/* Run a pipeline of n commands: c[0] | c[1] | ... | c[n-1].
 * In the background (&): don't wait for it. */
static void run(struct cmd *c, int n, int background)
{
    int pids[MAXPIPE];
    int in = -1;                /* read end of the pipe from the previous command */

    if (n == 1 && builtin(&c[0]))
        return;

    for (int i = 0; i < n; i++) {
        int fd[2] = { -1, -1 };
        if (i < n - 1 && pipe(fd) < 0) {
            fprintf(stderr, "sh: cannot make a pipe\n");
            n = i;
            break;
        }
        pids[i] = start(&c[i], in, fd[1], fd[0], background);
        if (pids[i] < 0) {
            fprintf(stderr, "sh: cannot fork\n");
            n = i;
            break;
        }
        /* The shell keeps no pipe ends: otherwise the reader would never
         * see end of file, since a writer (the shell) would still exist. */
        if (in >= 0)
            close(in);
        if (fd[1] >= 0)
            close(fd[1]);
        in = fd[0];
    }
    if (in >= 0)
        close(in);
    if (n == 0)
        return;

    if (background) {
        fprintf(stderr, "%d\n", pids[n - 1]);    /* V7 sh prints the last pid */
        return;
    }
    /* Wait for every command of the pipeline; report the last one. */
    for (int left = n; left > 0; ) {
        int status, w = wait_child(&status);
        if (w == -1)
            break;
        for (int i = 0; i < n; i++) {
            if (w == pids[i]) {
                left--;
                if (i == n - 1) {
                    report(status);
                    last_status = (status >> 8) & 0377;
                }
            }
        }
        /* (other pids: background jobs that ended meanwhile) */
    }
}

/* Run every pipeline on a line. */
static void run_line(char *line)
{
    static char words[LINE_MAX];
    static struct cmd c[MAXPIPE];
    struct lexer lx = { line, words, NULL };
    enum token t;

    do {
        int n = 0;
        do {                    /* collect one pipeline */
            if (n == MAXPIPE) {
                fprintf(stderr, "sh: pipeline too long\n");
                return;
            }
            t = parse(&lx, &c[n++]);
            if (t == BAD)
                return;
            if (t == PIPE && !c[n - 1].argv[0]) {
                fprintf(stderr, "sh: syntax error: command expected before |\n");
                return;
            }
        } while (t == PIPE);
        if (!c[n - 1].argv[0]) {
            if (n > 1) {
                fprintf(stderr, "sh: syntax error: command expected after |\n");
                return;
            }
            continue;           /* empty command */
        }
        run(c, n, t == AMP);
    } while (t == SEMI || t == AMP);
}

/* ^C while the shell itself is waiting: forget the line being typed.
 * (The terminal already threw it away; the read() returns EINTR.) */
static int on_interrupt(int sig)
{
    signal(sig, on_interrupt);  /* V7 handlers are one-shot: re-arm */
    interrupted = 1;
    return 0;
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
    interactive = in == stdin && isatty(0);
    if (interactive) {
        signal(SIGINT, on_interrupt);
        signal(SIGQUIT, SIG_IGN);
        signal(SIGTERM, SIG_IGN);   /* "kill 0" kills your jobs, not you */
    }

    for (;;) {
        if (interactive)
            fputs("$ ", stderr);
        if (!fgets(line, sizeof(line), in)) {
            if (interrupted) {  /* ^C: a new prompt, not the end */
                interrupted = 0;
                clearerr(in);
                fputc('\n', stderr);
                continue;
            }
            break;              /* end of file: ^D on a terminal */
        }
        interrupted = 0;
        run_line(line);
    }
    return last_status;
}
