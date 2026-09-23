#!/usr/bin/env bash
# Look at: the inode number and the link count in stat, the same inode number for both names in ls -i,
# the IUse% column of df -i, and the physical_offset and length columns of filefrag (one row per extent).
set -eu

work="$(mktemp -d)"
cd "$work"

# The inode of a file: number, link count, size, blocks (typical)
echo "hello filesystem" > report.txt
stat report.txt
#   Size: 17   Blocks: 8   IO Block: 4096   regular file
#   Inode: 1317042   Links: 1
# A hard link: two names, one inode number
ln report.txt alias.txt
ls -i report.txt alias.txt
#   1317042 alias.txt   1317042 report.txt
# Inodes are a fixed budget; extents are the runs of disk blocks
df -i .
filefrag -v report.txt || echo "filefrag is part of e2fsprogs and may need sudo on this filesystem"

cd /
rm -rf "$work"
