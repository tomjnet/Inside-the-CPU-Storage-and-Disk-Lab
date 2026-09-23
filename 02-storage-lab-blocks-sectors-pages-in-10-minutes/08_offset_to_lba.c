/* Blocks, Sectors and Pages: How Storage Is Organized - slide 8: from byte offset to lba and block (C version of 08_offset_to_lba.cpp) */
/* Build: make 08_offset_to_lba_c */
#if defined(__linux__)
#define _GNU_SOURCE                  /* sched_setaffinity, CPU_SET, O_DIRECT, clock_gettime */
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS      /* fopen, strerror: MSVC deprecates the standard names */
#endif

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

static const uint64_t kSector = 512;    /* logical sector, bytes */
static const uint64_t kBlock  = 4096;   /* filesystem block, bytes */
typedef struct { uint64_t lba, in_sector, block, in_block; } Where;

/* pure arithmetic: no system call, no device access */
static Where locate(uint64_t part_start_lba, uint64_t offset) {
    Where w;
    w.lba       = part_start_lba + offset / kSector;
    w.in_sector = offset % kSector;
    w.block     = offset / kBlock;        /* 8 sectors per block */
    w.in_block  = offset % kBlock;        /* 0 means block aligned */
    return w;
}

int main(void) {
    const uint64_t part_start_lba = 2048;   /* the usual first partition: 1 MiB into the device */
    const uint64_t offsets[] = {0, 511, 512, 4095, 4096, 8192, 9216, 10000, 1048576};
    const size_t count = sizeof offsets / sizeof offsets[0];

    printf("partition starts at LBA %" PRIu64 " = byte %" PRIu64 " of the device\n",
           part_start_lba, part_start_lba * kSector);
    printf("sector %" PRIu64 " B, block %" PRIu64 " B, %" PRIu64 " sectors per block\n\n",
           kSector, kBlock, kBlock / kSector);
    printf("%10s%8s%11s%8s%10s  aligned\n", "offset", "LBA", "in sector", "block", "in block");

    int ok = 1;
    for (size_t i = 0; i < count; ++i) {
        const uint64_t offset = offsets[i];
        const Where w = locate(part_start_lba, offset);
        printf("%10" PRIu64 "%8" PRIu64 "%11" PRIu64 "%8" PRIu64 "%10" PRIu64 "  %s\n",
               offset, w.lba, w.in_sector, w.block, w.in_block,
               w.in_block == 0 ? "block" : (w.in_sector == 0 ? "sector only" : "no"));
        /* the way back must give the same byte: LBA and remainder rebuild the offset */
        const uint64_t back = (w.lba - part_start_lba) * kSector + w.in_sector;
        if (back != offset || w.block * kBlock + w.in_block != offset) ok = 0;
    }

    const Where hand = locate(part_start_lba, 10000);   /* the one the video does by hand */
    printf("\noffset 10000: sector %" PRIu64 " of the partition, LBA %" PRIu64 ", block %" PRIu64 "\n",
           (uint64_t)10000 / kSector, hand.lba, hand.block);
    printf(ok ? "round trip check passed\n" : "round trip check FAILED\n");
    return ok ? 0 : 1;
}
