#!/usr/bin/env bash
# Three fio jobs on one test file: read the clat percentiles of qd1, the IOPS of qd32 and the BW of seq.
# Needs fio (sudo apt install fio) and 1 GiB free under /tmp; run it on real Linux hardware for honest numbers.
set -eu

# common: a 1 GiB test file, page cache bypassed, 30 seconds each
F="--filename=/tmp/fio.dat --size=1G --direct=1 --ioengine=libaio"
T="--runtime=30 --time_based --group_reporting"
# latency of one I/O: 4k random read at queue depth 1
fio --name=qd1 $F $T --rw=randread --bs=4k --iodepth=1
# IOPS of the device: the same job at queue depth 32
fio --name=qd32 $F $T --rw=randread --bs=4k --iodepth=32
# throughput: 1 MiB sequential read
fio --name=seq $F $T --rw=read --bs=1M --iodepth=8
# remove the test file
rm -f /tmp/fio.dat
