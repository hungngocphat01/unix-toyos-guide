/* perm.c -- permissions, V7's rules (V7: fio.c access() and owner()).
 *
 * Every file has an owner, a group, and 9 permission bits:
 *
 *    owner   group   others
 *    r w x   r w x   r w x        e.g. 0754 = rwx r-x r--
 *
 * Exactly ONE triple applies: the owner's if you are the owner, else the
 * group's if you are in the group, else the others'. The superuser (uid 0)
 * may do anything, except execute a file with no x bit at all.
 */
#include "fs/fs.h"
#include "proc/proc.h"
#include "abi/errno.h"

/* May the current process read (S_IREAD), write (S_IWRITE) or execute /
 * search (S_IEXEC) ip? mode is one of the owner bits: 0400, 0200, 0100. */
int iaccess(struct inode *ip, int mode)
{
    if (current->uid == 0) {
        if (mode == S_IEXEC && !IS_DIR(ip) && !(ip->d.mode & 0111))
            return -EACCES;
        return 0;
    }
    if (current->uid != ip->d.uid) {
        mode >>= 3;                     /* the group triple */
        if (current->gid != ip->d.gid)
            mode >>= 3;                 /* the others triple */
    }
    return (ip->d.mode & mode) ? 0 : -EACCES;
}

/* May the current process change ip's attributes (chmod, utime)? */
int iowner(struct inode *ip)
{
    return current->uid == 0 || current->uid == ip->d.uid ? 0 : -EPERM;
}

int suser(void)
{
    return current->uid == 0 ? 0 : -EPERM;
}
