#!/usr/bin/env bash
# Computer Storage in 5 Minutes: HDD, SSD and NVMe - slide 5: which disks do you have? lsblk
# Look at ROTA (1 = spinning platters, 0 = flash) and TRAN (sata or nvme) of every whole disk.
# Inside WSL or a virtual machine you only see virtual disks: run it on real Linux hardware.
set -eu

# one line per whole disk: ROTA 1 means it spins, TRAN is the bus
lsblk -d -o NAME,ROTA,TRAN,SIZE
#   NAME    ROTA TRAN   SIZE      (typical output)
#   sda        1 sata   3.6T      HDD: platters behind SATA
#   sdb        0 sata 931.5G      SATA SSD: flash behind AHCI
#   nvme0n1    0 nvme   1.8T      NVMe: flash on PCIe

# the same flag straight from sysfs, one file per device
grep . /sys/block/*/queue/rotational
#   /sys/block/nvme0n1/queue/rotational:0
#   /sys/block/sda/queue/rotational:1
