/* abi/stat.h -- file types/permissions and struct stat, as in V7.
 * The S_IF* values are also exactly ext2's i_mode bits. */
#ifndef ABI_STAT_H
#define ABI_STAT_H

#include "abi/types.h"

struct stat {
    dev_t          st_dev;
    ino_t          st_ino;
    unsigned short st_mode;
    short          st_nlink;
    short          st_uid;
    short          st_gid;
    dev_t          st_rdev;     /* device number, for device files */
    off_t          st_size;
    time_t         st_atime;
    time_t         st_mtime;
    time_t         st_ctime;
};

#define S_IFMT   0170000        /* type of file:       */
#define S_IFDIR  0040000        /*   directory         */
#define S_IFCHR  0020000        /*   character device  */
#define S_IFBLK  0060000        /*   block device      */
#define S_IFREG  0100000        /*   regular file      */
#define S_IFIFO  0010000        /*   pipe (not in V7)  */
#define S_ISUID  0004000        /* set user id on execution */
#define S_ISGID  0002000        /* set group id on execution */
#define S_ISVTX  0001000        /* sticky */
#define S_IREAD  0000400        /* owner may read      */
#define S_IWRITE 0000200        /* owner may write     */
#define S_IEXEC  0000100        /* owner may execute   */

#endif
