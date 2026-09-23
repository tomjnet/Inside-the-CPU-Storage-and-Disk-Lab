#!/usr/bin/env bash
# Look at: the calls column of strace (the same on both paths, only the time differs), the Dirty line of
# /proc/meminfo (it grows with buffered writes only), and the IOPS and clat percentiles of the two fio runs.
# Tools: sudo apt install strace fio. Build the sample first: make 07_buffered_vs_direct
set -eu

# count the system calls: same count on both paths, different time
strace -c -e trace=pwrite64,fsync ./07_buffered_vs_direct
# the page cache made visible: Dirty grows only with buffered writes
grep -E '^(Cached|Dirty|Writeback):' /proc/meminfo
#   Cached:    5123456 kB     (typical)
#   Dirty:         184 kB
#   Writeback:       0 kB
# the same job on both paths: 4k random writes, queue depth 1
job="--filename=/tmp/fio.dat --size=64M --rw=randwrite --bs=4k"
fio --name=buffered $job --direct=0
fio --name=direct $job --direct=1

rm -f /tmp/fio.dat
