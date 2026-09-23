/* Buffered I/O vs Direct I/O: How Low Latency Storage Works - slide 9: the application caches itself (C version of 09_app_cache.cpp) */
/* Build: make 09_app_cache_c */
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

/* ---- timing: now_ns from the timing harness ---- */
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static double now_ns(void) {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1e9 / (double)f.QuadPart;
}
#else
#include <time.h>
static double now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}
#endif

#define K_BLOCK_SIZE 4096
#define K_BLOCKS 64                    /* the "device": a 256 KiB temporary file */
typedef struct { char data[K_BLOCK_SIZE]; } Block;

static char g_device[512];
static uint64_t g_device_reads = 0;

/* One read of one block from the device. The portable sample goes through fopen and fread, so the operating
   system still caches it; with an O_DIRECT descriptor (06_aligned_blocks.c) this is a real device read. */
static void read_block(uint64_t n, Block *b) {
    memset(b->data, 0, sizeof b->data);
    FILE *in = fopen(g_device, "rb");
    if (in != NULL) {
        if (fseek(in, (long)(n * K_BLOCK_SIZE), SEEK_SET) != 0 || fread(b->data, 1, sizeof b->data, in) != sizeof b->data)
            memset(b->data, 0, sizeof b->data);   /* a short read leaves zeros: the check in main() catches it */
        fclose(in);
    }
    ++g_device_reads;
}

/* with O_DIRECT nobody caches for us: the application keeps its own.
   C has no std::unordered_map: block numbers are small, so a table indexed by block number does the job. */
typedef struct {
    Block *blocks[K_BLOCKS];           /* by block number, NULL = not cached */
    size_t cached;
    uint64_t hits, misses;
} BlockCache;

static const Block *cache_get(BlockCache *c, uint64_t n) {
    if (c->blocks[n] != NULL) { ++c->hits; return c->blocks[n]; }   /* RAM */
    ++c->misses;                       /* one direct read: the device */
    Block *b = malloc(sizeof *b);
    if (b == NULL) return NULL;
    read_block(n, b);
    c->blocks[n] = b;
    ++c->cached;
    return b;
}

/* fixed-seed xorshift64 in place of std::mt19937(42): the counts differ a little from the C++ run */
static uint64_t g_rng = 42;
static uint64_t next_random(void) {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 7;
    g_rng ^= g_rng << 17;
    return g_rng;
}

int main(void) {
    const char *tmpdir = NULL;
#if defined(_WIN32)
    tmpdir = getenv("TEMP");
#else
    tmpdir = "/tmp";
#endif
    if (tmpdir == NULL || tmpdir[0] == '\0') tmpdir = ".";
    snprintf(g_device, sizeof g_device, "%s/storage-lab-08-cache-%.0f.dat", tmpdir, now_ns());
    {
        FILE *out = fopen(g_device, "wb");
        if (out == NULL) {
            printf("cannot create %s\n", g_device);
            return 1;
        }
        Block b;
        for (uint64_t n = 0; n < K_BLOCKS; ++n) {
            memset(b.data, (char)('A' + n % 26), sizeof b.data);   /* block n is full of one letter */
            fwrite(b.data, 1, sizeof b.data, out);
        }
        fclose(out);
    }

    /* A database shaped pattern: 8 hot blocks (index roots) take 80 percent of the reads, the rest is uniform. */
    BlockCache cache;
    memset(&cache, 0, sizeof cache);
    int ok = 1;
    double hit_ns = 0, miss_ns = 0;
    enum { kReads = 20000 };
    for (int i = 0; i < kReads; ++i) {
        const uint64_t n = next_random() % 100 < 80 ? next_random() % 8 : next_random() % K_BLOCKS;
        const uint64_t before = cache.misses;
        const double t0 = now_ns();
        const Block *b = cache_get(&cache, n);
        const double ns = now_ns() - t0;
        if (cache.misses != before) miss_ns += ns; else hit_ns += ns;
        if (b == NULL || b->data[0] != (char)('A' + n % 26) || b->data[K_BLOCK_SIZE - 1] != b->data[0]) ok = 0;
    }
    remove(g_device);

    printf("%d reads of %d blocks, 80 percent of them on 8 hot blocks\n", kReads, K_BLOCKS);
    printf("  hits         : %" PRIu64 "\n", cache.hits);
    printf("  misses       : %" PRIu64 " (each one is one device read: %" PRIu64 ")\n", cache.misses, g_device_reads);
    printf("  hit rate     : %.1f percent\n", 100.0 * (double)cache.hits / kReads);
    printf("  cached bytes : %lu\n", (unsigned long)(cache.cached * K_BLOCK_SIZE));
    if (cache.hits > 0 && cache.misses > 0) {
        printf("  average hit  : %.1f ns (this machine)\n", hit_ns / (double)cache.hits);
        printf("  average miss : %.1f ns (this machine, through the\n"
               "                 operating system cache: a real O_DIRECT miss on NVMe is about 100000 ns)\n",
               miss_ns / (double)cache.misses);
    }
    printf("without the cache every one of the %d reads would have gone to the device\n"
           "this model never evicts: a real buffer pool has a size limit, an eviction policy (LRU, clock),\n"
           "its own read ahead and its own batching of dirty blocks\n", kReads);
    for (int n = 0; n < K_BLOCKS; ++n) free(cache.blocks[n]);   /* no destructor in C: free by hand */
    if (!ok) {
        printf("FAIL: a cached block does not hold the data of its block number\n");
        return 1;
    }
    return 0;
}
