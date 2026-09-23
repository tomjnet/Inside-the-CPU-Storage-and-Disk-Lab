#!/usr/bin/env bash
# Linux Page Cache: Why Disk I/O Often Never Touches the Disk Immediately - slide 9: watching the cache from the shell.
# Look at buff/cache in free, at Dirty while a big copy runs in another terminal, and at the last line: it needs root.
set -eu

# how much RAM is page cache: the buff/cache column (typical)
free -h
#          total   used   free  shared  buff/cache  available
#   Mem:    31Gi  6.2Gi   11Gi   410Mi        14Gi       24Gi
# pages waiting for writeback, and the limits
grep -E '^(Cached|Dirty|Writeback):' /proc/meminfo
sysctl vm.dirty_background_ratio vm.dirty_ratio
# how much of one file is in the cache
fincore /bin/bash
# flush every dirty page, then drop the clean cache: needs root
sync
echo 3 | sudo tee /proc/sys/vm/drop_caches
