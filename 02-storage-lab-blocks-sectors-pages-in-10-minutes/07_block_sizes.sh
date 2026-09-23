#!/usr/bin/env bash
# Blocks, Sectors and Pages: How Storage Is Organized - slide 7: read the sizes from /sys
# Look at the logical and the physical sector size of your disk, then at the filesystem block and the
# memory page: on most machines the last three are 4096. Pass your disk as the first argument
# (default nvme0n1; "lsblk -d" lists them). Inside WSL you only see virtual disks.
set -eu
DEV="${1:-nvme0n1}"
if [ ! -d "/sys/block/$DEV" ]; then
    echo "no /sys/block/$DEV here: pass one of these as the first argument"
    lsblk -d -o NAME
    exit 0
fi

# the device's unit: logical and physical sector size, in bytes
cat "/sys/block/$DEV/queue/logical_block_size"
cat "/sys/block/$DEV/queue/physical_block_size"
#   512
#   512      typical NVMe; a 512e hard disk prints 512 and 4096
# every disk at once, plus the I/O sizes the kernel prefers
lsblk -d -o NAME,LOG-SEC,PHY-SEC,MIN-IO,OPT-IO
# the filesystem block and the memory page: no root needed
stat -f -c 'filesystem block: %S bytes' .
getconf PAGESIZE
#   filesystem block: 4096 bytes
#   4096
