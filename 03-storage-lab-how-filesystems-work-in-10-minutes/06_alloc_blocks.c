/* How Filesystems Work: From File Name to Disk Blocks - slide 6: free space: the bitmaps (C version of 06_alloc_blocks.cpp) */
/* Build: make 06_alloc_blocks_c */
#if defined(__linux__)
#define _GNU_SOURCE                  /* sched_setaffinity, CPU_SET, O_DIRECT, clock_gettime */
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS      /* fopen, strerror: MSVC deprecates the standard names */
#endif

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct { uint32_t start; uint32_t count; } Extent;  /* a run of blocks */

/* The part of the tiny filesystem this slide needs: the block bitmap and its allocator.
   C has no std::vector<bool>: one byte per block, with its length next to it. */
#define NBLOCKS 32
typedef struct {
    unsigned char used[NBLOCKS];                    /* one entry per block: 1 = used */
    uint32_t n;
} TinyFs;

/* ---- the slide ---- */
/* First fit on the block bitmap: one bit per block, O(blocks) scan. */
static Extent fs_alloc(TinyFs *fs, uint32_t want) {
    uint32_t b = 0, n = fs->n;
    while (b < n && fs->used[b]) ++b;          /* first free block */
    Extent e = {b, 0};
    for (; b < n && !fs->used[b] && e.count < want; ++b, ++e.count)
        fs->used[b] = 1;                       /* grow the run */
    return e;        /* shorter than want: the file fragments */
}
/* ---- end of the slide ---- */

/* writes the bitmap into out, 8 blocks per group */
static void bits(const TinyFs *fs, char *out, size_t cap) {
    size_t k = 0;
    for (uint32_t i = 0; i < fs->n && k + 2 < cap; ++i) {
        if (i > 0 && i % 8 == 0) out[k++] = ' ';
        out[k++] = fs->used[i] ? '1' : '0';
    }
    out[k] = '\0';
}

int main(void) {
    /* The bitmap of the slide's figure: blocks 0 to 9 and 13 to 19 are used, a hole of 3 at block 10. */
    TinyFs fs = {{0}, NBLOCKS};
    for (uint32_t b = 0; b < 10; ++b) fs.used[b] = 1;
    for (uint32_t b = 13; b < 20; ++b) fs.used[b] = 1;

    char line[64];
    printf("block bitmap, 1 = used, 0 = free (32 blocks, 8 per group)\n");
    bits(&fs, line, sizeof line);
    printf("  before      %s\n", line);

    /* One request for 5 blocks: the caller keeps asking until it is covered, as create() does on slide 8. */
    Extent got[NBLOCKS];
    size_t n_got = 0;
    uint32_t need = 5;
    while (need > 0) {
        Extent e = fs_alloc(&fs, need);
        if (e.count == 0) break;                   /* disk full: a real filesystem returns ENOSPC */
        printf("  alloc(%" PRIu32 ") -> extent: start %" PRIu32 ", count %" PRIu32 "\n", need, e.start, e.count);
        got[n_got++] = e;
        need -= e.count;
    }
    bits(&fs, line, sizeof line);
    printf("  after       %s\n", line);
    printf("  one request for 5 blocks came back as %zu extents: the file is fragmented\n\n", n_got);

    /* How far one bitmap block reaches on a real filesystem. */
    const uint64_t block = 4096;
    const uint64_t bits_per_block = block * 8;
    printf("one 4 KiB bitmap block = %" PRIu64 " bits = %" PRIu64 " MiB of disk (one ext4 block group)\n",
           bits_per_block, bits_per_block * block / (1024 * 1024));

    const int ok = n_got == 2 && got[0].start == 10 && got[0].count == 3 && got[1].start == 20 &&
                   got[1].count == 2;
    if (!ok) {
        printf("FAIL: first fit did not return the extents of the slide\n");
        return 1;
    }
    return 0;
}
