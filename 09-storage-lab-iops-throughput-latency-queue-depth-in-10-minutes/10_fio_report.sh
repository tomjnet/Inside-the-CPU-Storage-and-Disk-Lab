#!/usr/bin/env bash
# One fio job and the lines of its report that matter: IOPS and BW, the clat percentiles, the IO depths.
# Check your own report with both formulas: IOPS x block size = BW, and IOPS x average latency = iodepth.
set -eu

# the common options of 09_fio_jobs.sh
F="--filename=/tmp/fio.dat --size=1G --direct=1 --ioengine=libaio"
T="--runtime=30 --time_based --group_reporting"

# what to read in the fio report (typical NVMe, yours will differ)
fio --name=qd32 $F $T --rw=randread --bs=4k --iodepth=32
#   read: IOPS=412k, BW=1609MiB/s (1687MB/s)
#     clat (usec): min=38, max=9120, avg=76.9, stdev=41.2
#     clat percentiles (usec):
#      | 50.00th=[   72], 99.00th=[  143], 99.90th=[  378]
#   IO depths : 32=100.0%
# check 1: 412k x 4 KiB = 1609 MiB/s
# check 2: 412k x 77 us = 32 in flight, Little's law

rm -f /tmp/fio.dat
