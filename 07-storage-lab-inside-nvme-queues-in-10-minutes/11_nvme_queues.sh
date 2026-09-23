#!/usr/bin/env bash
# Inside NVMe: Submission Queues, Completion Queues and PCIe - slide 11: the real queues on Linux
# Look at: how many directories mq holds (one per hardware queue, usually one per core), which CPUs feed
# queue 0, and the nvme0qN lines of /proc/interrupts (nvme0q0 is the admin queue, one MSI-X vector each).
set -eu

# WSL and most virtual machines show no NVMe device: this script is for real hardware
if [ ! -e /sys/block/nvme0n1 ]; then
    echo "no /sys/block/nvme0n1 here: run this on real Linux hardware with an NVMe drive"
    exit 0
fi

# devices and namespaces (nvme-cli, real hardware, not WSL)
sudo nvme list
#   /dev/nvme0n1  1TB NVMe SSD  ns 1  1.00 TB   (typical, trimmed)
# one directory per hardware queue the kernel created
ls /sys/block/nvme0n1/mq
#   0  1  2  3  4  5  6  7          (typical: one per core)
# the CPUs that submit to queue 0
cat /sys/block/nvme0n1/mq/0/cpu_list
#   0                                (typical)
# one MSI-X vector per queue: nvme0q0 is the admin queue
grep nvme /proc/interrupts
#   45:  1210  0  0  0  PCI-MSIX  nvme0q1   (typical, trimmed)
