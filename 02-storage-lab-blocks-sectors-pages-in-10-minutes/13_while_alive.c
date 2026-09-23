/* Blocks, Sectors and Pages: How Storage Is Organized - slide 13: thank you (C version of 13_while_alive.cpp) */
/* Build: make 13_while_alive_c */
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

/* The imaginary "tomjnet.h" of the slide, written out so the program builds. */
static const uint64_t kSector = 512;
static const uint64_t kBlock = 4096;
static const uint64_t kPartitionStart = 2048;              /* 1 MiB in: aligned */
typedef uint64_t Lba;
typedef struct { const char *name; } Topic;

/* C has no std::vector: a pointer, a length, a capacity, and disk_free at the end */
typedef struct {
    uint64_t block_size;
    Topic *blocks;                                         /* one topic per filesystem block */
    size_t size, capacity;
} Disk;

static int disk_append(Disk *d, Topic t, Lba *out) {       /* gives the first LBA of the new block */
    if (d->size == d->capacity) {
        size_t cap = d->capacity ? d->capacity * 2 : 4;
        Topic *grown = realloc(d->blocks, cap * sizeof *grown);
        if (grown == NULL) return 0;
        d->blocks = grown;
        d->capacity = cap;
    }
    d->blocks[d->size++] = t;
    *out = kPartitionStart + (uint64_t)(d->size - 1) * (d->block_size / kSector);
    return 1;
}
static const Topic *disk_read(const Disk *d, Lba lba) {
    return &d->blocks[(size_t)((lba - kPartitionStart) / (d->block_size / kSector))];
}
static void disk_free(Disk *d) { free(d->blocks); d->blocks = NULL; d->size = d->capacity = 0; }

static size_t episodes = 0;
static int alive(void) { return episodes < 3; }            /* three episodes, then the demo ends */
static Disk format(uint64_t block_size) {
    printf("formatted: %" PRIu64 " byte blocks, partition starts at LBA %" PRIu64 "\n", block_size, kPartitionStart);
    Disk d = {block_size, NULL, 0, 0};
    return d;
}
static Topic next_storage_topic(void) {
    static const char *topics[] = {"How Filesystems Work: From File Name to Disk Blocks",
                                   "Read and Write in C++: What Happens Behind the System Call",
                                   "Linux Page Cache: Why Disk I/O Often Never Touches the Disk Immediately"};
    Topic t = {topics[episodes++]};
    return t;
}
static void learn(const Topic *t) { printf("  learned: %s\n", t->name); }
static void subscribe(void) { printf("  subscribed to TomJNet\n"); }

int main(void) {
    Disk channel = format(kBlock);       /* 4 KiB blocks, aligned */
    int rc = 0;
    while (alive()) {
        Lba next = 0;
        if (!disk_append(&channel, next_storage_topic(), &next)) { printf("out of memory\n"); rc = 1; break; }
        printf("block written at LBA %" PRIu64 "\n", next);
        learn(disk_read(&channel, next));  /* one block at a time */
        subscribe();                       /* written once, kept forever */
    }
    disk_free(&channel);
    return rc;
}
