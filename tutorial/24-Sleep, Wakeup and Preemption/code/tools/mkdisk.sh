#!/bin/sh
# mkdisk.sh OUTPUT PROGRAM...
#
# Build an ext2 disk image containing:
#   - everything under root/ (the files of the repo's root/ directory)
#   - each PROGRAM, in /bin
# Everything is owned by root (uid 0), whatever your uid on the host is.
set -e

out=$1
shift

# e2fsprogs from Homebrew is "keg-only": its tools are not on the PATH.
PATH="/opt/homebrew/opt/e2fsprogs/sbin:/usr/local/opt/e2fsprogs/sbin:$PATH"

staging=$(dirname "$out")/root
rm -rf "$staging"
mkdir -p "$staging/bin" "$staging/dev" "$staging/tmp" "$staging/usr/ken"
cp -R root/. "$staging/"
cp "$@" "$staging/bin/"

# 8 MiB, 1 KiB blocks, 128-byte inodes, and no optional features except
# "filetype" (a type byte in directory entries): the simplest ext2 there is.
rm -f "$out"
mke2fs -q -t ext2 -b 1024 -I 128 \
       -O ^dir_index,^ext_attr,^resize_inode,^sparse_super,^large_file \
       -E root_owner=0:0 -d "$staging" "$out" 8M >/dev/null 2>&1

# Device files (debugfs can create them without root privileges on the host).
# Major numbers: kernel/dev/dev.h.
debugfs -w -f - "$out" >/dev/null 2>&1 <<EOT
cd /dev
mknod console c 0 0
set_inode_field console mode 020622
mknod null c 1 0
set_inode_field null mode 020666
EOT

# Make every file owned by root.
(cd "$staging" && find . -mindepth 1 | sed 's|^\.||') | while read -r path; do
    echo "set_inode_field $path uid 0"
    echo "set_inode_field $path gid 0"
done | debugfs -w -f - "$out" >/dev/null 2>&1

# Permissions that matter (lesson 22), as V7 installed them:
#   /tmp is writable by everybody; ken (uid 100, group 10) owns his home;
#   these programs are setuid root: mkdir/rmdir/mv link and unlink
#   directories, su changes uid, passwd rewrites /etc/passwd.
debugfs -w -f - "$out" >/dev/null 2>&1 <<EOT
set_inode_field /tmp mode 040777
set_inode_field /usr/ken uid 100
set_inode_field /usr/ken gid 10
set_inode_field /bin/mkdir mode 0104755
set_inode_field /bin/rmdir mode 0104755
set_inode_field /bin/mv mode 0104755
set_inode_field /bin/su mode 0104755
set_inode_field /bin/passwd mode 0104755
EOT
