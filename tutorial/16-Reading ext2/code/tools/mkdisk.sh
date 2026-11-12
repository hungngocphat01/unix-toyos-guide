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
mkdir -p "$staging/bin" "$staging/dev" "$staging/tmp"
cp -R root/. "$staging/"
cp "$@" "$staging/bin/"

# 8 MiB, 1 KiB blocks, 128-byte inodes, and no optional features except
# "filetype" (a type byte in directory entries): the simplest ext2 there is.
rm -f "$out"
mke2fs -q -t ext2 -b 1024 -I 128 \
       -O ^dir_index,^ext_attr,^resize_inode,^sparse_super,^large_file \
       -E root_owner=0:0 -d "$staging" "$out" 8M 2>/dev/null

# Make every file owned by root.
(cd "$staging" && find . -mindepth 1 | sed 's|^\.||') | while read -r path; do
    echo "set_inode_field $path uid 0"
    echo "set_inode_field $path gid 0"
done | debugfs -w -f - "$out" >/dev/null 2>&1
