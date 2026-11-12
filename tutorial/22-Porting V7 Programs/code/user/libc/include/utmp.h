/* utmp.h -- who is logged in where (/etc/utmp), and the login history
 * (/usr/adm/wtmp). V7's format. */
#ifndef _UTMP_H
#define _UTMP_H

struct utmp {
    char ut_line[8];            /* terminal name, without /dev/ */
    char ut_name[8];            /* user name */
    long ut_time;               /* time logged in */
};

#endif
