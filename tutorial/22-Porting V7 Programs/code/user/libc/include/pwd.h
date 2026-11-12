/* pwd.h -- a line of /etc/passwd. */
#ifndef _PWD_H
#define _PWD_H

#include <sys/cdefs.h>

struct passwd {
    char *pw_name;
    char *pw_passwd;
    int   pw_uid;
    int   pw_gid;
    int   pw_quota;
    char *pw_comment;
    char *pw_gecos;
    char *pw_dir;
    char *pw_shell;
};

struct passwd *getpwent __P((void));
struct passwd *getpwuid __P((int));
struct passwd *getpwnam __P((const char *));
int            setpwent __P((void));   /* V7: implicit int */
int            endpwent __P((void));   /* V7: implicit int */

#endif
