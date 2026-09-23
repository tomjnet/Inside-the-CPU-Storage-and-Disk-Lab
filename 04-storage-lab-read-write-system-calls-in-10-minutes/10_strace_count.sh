#!/usr/bin/env bash
# Count the write system calls of the samples with strace (sudo apt install strace; build first with make).
# Look at the "calls" column: about a million for 07, 129 for 09, and at the 8192 in the last write line.
set -eu

# count the write calls; "once" = one pass, no timing loop
strace -c -e trace=write ./07_byte_at_a_time once
#   calls      syscall      (typical)
#   1048577    write        1 MiB of single bytes + 1 printed line
strace -c -e trace=write ./09_ofstream_put once
#   129        write        128 blocks of 8 KiB + 1 printed line
# without -c: every call with its size and its return value
strace -e trace=write ./09_ofstream_put once 2>&1 | head -3
#   write(3, "\0\0\0\0\0\0\0\0\0\0\0\0"..., 8192) = 8192
# strace stops the program at every call: count with it, never time
