/* Blocks, Sectors and Pages: How Storage Is Organized - slide 9: a misaligned write touches two blocks (C version of 09_misaligned_write.cpp) */
/* Build: make 09_misaligned_write_c */
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
#include <stdlib.h>
#include <string.h>

static const uint64_t kBlock = 4096;    /* filesystem block, bytes */

/* blocks a write touches: one device write each, plus one
   read for every block that is only partly overwritten */
static uint64_t blocks_touched(uint64_t offset, uint64_t len) {
    uint64_t first = offset / kBlock;
    uint64_t last  = (offset + len - 1) / kBlock;
    return last - first + 1;
}

/* A model block device: it can only read or write one whole block, and it counts both.
   C has no destructor: the owner calls device_free on every path. */
typedef struct {
    unsigned char *media;
    size_t size;
    uint64_t block_reads;
    uint64_t block_writes;
} ModelDevice;

static int device_init(ModelDevice *dev, uint64_t blocks) {
    dev->size = (size_t)(blocks * kBlock);
    dev->media = calloc(dev->size, 1);
    dev->block_reads = 0;
    dev->block_writes = 0;
    return dev->media != NULL;
}

static void device_free(ModelDevice *dev) {
    free(dev->media);
    dev->media = NULL;
}

/* what the owner of the block (page cache or firmware) must do for a byte range */
static void device_write(ModelDevice *dev, uint64_t offset, const unsigned char *src, uint64_t len) {
    unsigned char staging[4096];
    const uint64_t first = offset / kBlock;
    const uint64_t last = (offset + len - 1) / kBlock;
    for (uint64_t b = first; b <= last; ++b) {
        const uint64_t lo = offset > b * kBlock ? offset : b * kBlock;
        const uint64_t hi = offset + len < (b + 1) * kBlock ? offset + len : (b + 1) * kBlock;
        unsigned char *block_begin = dev->media + (size_t)(b * kBlock);
        memset(staging, 0, sizeof staging);
        if (hi - lo != kBlock) {                              /* partly overwritten: read the old block */
            memcpy(staging, block_begin, (size_t)kBlock);
            ++dev->block_reads;
        }
        memcpy(staging + (size_t)(lo - b * kBlock), src + (size_t)(lo - offset), (size_t)(hi - lo));   /* modify */
        memcpy(block_begin, staging, (size_t)kBlock);        /* write it back */
        ++dev->block_writes;
    }
}

static int run(const char *label, uint64_t offset, uint64_t len, uint64_t expected_blocks) {
    ModelDevice dev;
    int ok = 0;
    unsigned char *reference = NULL;
    unsigned char *payload = NULL;
    if (!device_init(&dev, 8)) { printf("  out of memory\n"); return 0; }
    reference = calloc(dev.size, 1);
    payload = malloc((size_t)len);
    if (reference == NULL || payload == NULL) { printf("  out of memory\n"); goto done; }
    memset(payload, 0xAB, (size_t)len);

    device_write(&dev, offset, payload, len);
    memset(reference + (size_t)offset, 0xAB, (size_t)len);

    const uint64_t touched = blocks_touched(offset, len);
    printf("%s: %" PRIu64 " bytes at offset %" PRIu64 "\n", label, len, offset);
    printf("  blocks touched %" PRIu64 " (first %" PRIu64 ", last %" PRIu64 ")\n",
           touched, offset / kBlock, (offset + len - 1) / kBlock);
    printf("  model device: %" PRIu64 " block reads, %" PRIu64 " block writes, %" PRIu64 " bytes moved\n",
           dev.block_reads, dev.block_writes, (dev.block_reads + dev.block_writes) * kBlock);
    ok = memcmp(dev.media, reference, dev.size) == 0 && touched == expected_blocks
         && dev.block_writes == touched;
    if (!ok) printf("  CHECK FAILED\n");
done:
    free(payload);
    free(reference);
    device_free(&dev);
    return ok;
}

int main(void) {
    uint64_t aligned    = blocks_touched(8192, 4096);        /* 1: block 2 */
    uint64_t misaligned = blocks_touched(8192 + 1024, 4096); /* 2: blocks 2, 3 */

    printf("blocks_touched(8192, 4096)        = %" PRIu64 "\n", aligned);
    printf("blocks_touched(8192 + 1024, 4096) = %" PRIu64 "\n\n", misaligned);

    int ok = aligned == 1 && misaligned == 2;
    ok = run("aligned write", 8192, 4096, 1) && ok;
    ok = run("misaligned write", 8192 + 1024, 4096, 2) && ok;
    ok = run("tiny write inside one block", 8192 + 100, 10, 1) && ok;
    ok = run("large aligned write", 4096, 4 * 4096, 4) && ok;

    printf("\nsame 4096 bytes from the program: the misaligned write costs 2 reads and 2 writes,\n"
           "the aligned one 0 reads and 1 write\n");
    return ok ? 0 : 1;
}
