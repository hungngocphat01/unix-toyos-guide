/* grp.h -- a line of /etc/group. */
#ifndef _GRP_H
#define _GRP_H

#include <sys/cdefs.h>

struct group {
    char  *gr_name;
    char  *gr_passwd;
    int    gr_gid;
    char **gr_mem;
};

struct group *getgrent __P((void));
struct group *getgrgid __P((int));
struct group *getgrnam __P((const char *));
void          setgrent __P((void));
void          endgrent __P((void));

#endif
