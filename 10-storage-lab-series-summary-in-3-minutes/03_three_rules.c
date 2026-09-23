/* Inside the CPU: What We Learned About Storage - slide 3: three rules: batch, know your cache, measure the tail (C version of 03_three_rules.cpp) */
/* Build: make 03_three_rules_c */
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

/* ---- timing harness: the C twin of bench_ns and sink ---- */
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

static volatile uint64_t g_sink = 0;
static void sink(uint64_t x) { g_sink = g_sink + x; }   /* keeps the result alive */

typedef struct { double min_ns; double median_ns; } BenchResult;

static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

/* warm up, repeat, keep the minimum and the median; at most 64 repeats */
static BenchResult bench_ns(void (*f)(void *), void *ctx, int warmup, int repeats) {
    double samples[64];
    if (repeats > 64) repeats = 64;
    for (int i = 0; i < warmup; ++i) f(ctx);
    for (int i = 0; i < repeats; ++i) {
        double t0 = now_ns();
        f(ctx);
        samples[i] = now_ns() - t0;
    }
    qsort(samples, (size_t)repeats, sizeof samples[0], cmp_double);
    BenchResult r = { samples[0], samples[repeats / 2] };
    return r;
}

#define K_DATA ((size_t)256 * 1024)   /* 4096 records of 64 B */
#define K_RECORD ((size_t)64)
#define K_BLOCK ((size_t)4096)

/* fixed-seed xorshift64: every run does the same work (the offsets differ from the C++ run) */
static uint64_t g_rng = 12345;
static uint64_t xorshift64(void) {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 7;
    g_rng ^= g_rng << 17;
    return g_rng;
}

/* reads the whole file in 64 KiB pieces and returns the number of bytes it saw */
static uint64_t read_all(const char *path) {
    static char buf[64 * 1024];
    uint64_t bytes = 0;
    FILE *in = fopen(path, "rb");
    if (in == NULL) return 0;
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, in)) > 0) bytes += (uint64_t)n;
    fclose(in);
    sink(bytes);
    return bytes;
}

/* 4 KiB reads at random block offsets, one at a time (queue depth 1): one latency in nanoseconds per read.
   Writes at most `reads` latencies into lat and returns how many it wrote. */
static size_t random_read_latencies(const char *path, uint64_t file_bytes, int reads, double *lat) {
    static char block[K_BLOCK];
    size_t count = 0;
    FILE *in = fopen(path, "rb");
    if (in == NULL) return 0;
    const uint64_t blocks = file_bytes / K_BLOCK;
    for (int i = 0; i < reads && blocks > 0; ++i) {
        const uint64_t lba = xorshift64() % blocks;
        double t0 = now_ns();
        int ok = fseek(in, (long)(lba * K_BLOCK), SEEK_SET) == 0 && fread(block, 1, K_BLOCK, in) == K_BLOCK;
        double t1 = now_ns();
        if (!ok) break;
        sink((uint64_t)(unsigned char)block[0]);
        lat[count++] = t1 - t0;
    }
    fclose(in);
    return count;
}

/* the lambdas of the C++ version become one context struct plus a function per benchmark */
typedef struct {
    FILE *out;
    const char *data;
    const char *path;
    int written;         /* stays 1 while every fwrite and fflush succeeds */
} Ctx;

static void tiny_writes(void *p) {       /* rule 1: 4096 system calls */
    Ctx *c = (Ctx *)p;
    for (size_t i = 0; i < K_DATA; i += K_RECORD) {
        if (fwrite(c->data + i, 1, K_RECORD, c->out) != K_RECORD || fflush(c->out) != 0) c->written = 0;
    }
}

static void batch_write(void *p) {       /* batched: one system call */
    Ctx *c = (Ctx *)p;
    if (fwrite(c->data, 1, K_DATA, c->out) != K_DATA || fflush(c->out) != 0) c->written = 0;
}

static void warm_read(void *p) {         /* rule 2: page cache: RAM speed, not the device */
    Ctx *c = (Ctx *)p;
    read_all(c->path);
}

static uint64_t file_size(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) return 0;
    long n = -1;
    if (fseek(f, 0, SEEK_END) == 0) n = ftell(f);
    fclose(f);
    return n < 0 ? 0 : (uint64_t)n;
}

int main(void) {
    const char *tmp = NULL;
#if defined(_WIN32)
    tmp = getenv("TEMP");
#else
    tmp = "/tmp";
#endif
    if (tmp == NULL || tmp[0] == '\0') tmp = ".";
    char path[1024];
    snprintf(path, sizeof path, "%s/tomjnet_storage_lab_10_three_rules.bin", tmp);

    FILE *out = fopen(path, "wb");
    if (out == NULL) {
        printf("cannot create %s: nothing to measure here\n", path);
        return 0;
    }

    char *data = malloc(K_DATA);
    double *lat = malloc(2000 * sizeof *lat);
    if (data == NULL || lat == NULL) {
        printf("out of memory\n");
        free(data);
        free(lat);
        fclose(out);
        remove(path);
        return 1;
    }
    memset(data, 'x', K_DATA);

    Ctx ctx = { out, data, path, 1 };
    BenchResult tiny = bench_ns(tiny_writes, &ctx, 3, 21);
    BenchResult batch = bench_ns(batch_write, &ctx, 3, 21);
    BenchResult warm = bench_ns(warm_read, &ctx, 3, 21);

    const int no_error = ctx.written && !ferror(out);
    const int written = (fclose(out) == 0) && no_error;  /* C has no destructor: close by hand */
    const uint64_t file_bytes = file_size(path);
    const uint64_t seen = read_all(path);
    size_t n = random_read_latencies(path, file_bytes, 2000, lat);
    if (n == 0) lat[n++] = 0.0;

    qsort(lat, n, sizeof lat[0], cmp_double);   /* rule 3: 4 KiB random I/O */
    double p50 = lat[n / 2];                    /* at queue depth 1: median */
    double p999 = lat[n * 999 / 1000];          /* and the tail, p99.9 */
    double p99 = lat[n * 99 / 100];

    const uint64_t expected = 2ull * 24ull * 262144ull;  /* two benchmarks, 3 warm up runs plus 21 repeats each */
    printf("file: %s, %" PRIu64 " KiB\n\n", path, file_bytes / 1024);

    printf("rule 1: batch system calls (256 KiB per run, this machine)\n");
    printf("  4096 writes of 64 B, flush after each: median %g ms, min %g ms\n",
           tiny.median_ns / 1e6, tiny.min_ns / 1e6);
    printf("  one write of 256 KiB, one flush:        median %g ms, min %g ms\n",
           batch.median_ns / 1e6, batch.min_ns / 1e6);
    if (batch.median_ns > 0.0) {
        printf("  ratio: %gx, this machine: the bytes are the same, the mode switches are not\n",
               tiny.median_ns / batch.median_ns);
    }

    printf("\nrule 2: know when you are hitting the page cache\n");
    const double mib = (double)seen / (1024.0 * 1024.0);
    printf("  warm read of %g MiB we just wrote: median %g ms", mib, warm.median_ns / 1e6);
    if (warm.median_ns > 0.0) printf(", about %g GiB per second", mib / (warm.median_ns / 1e9) / 1024.0);
    printf("\n  that is memory speed, not your SSD: the pages never left RAM\n");
    printf("  to reach the device: O_DIRECT (episode 8) or fio with direct=1 (episode 9)\n");

    printf("\nrule 3: measure tails at the queue depth you run (%d random 4 KiB reads, queue depth 1)\n", (int)n);
    printf("  p50 %g us, p99 %g us, p99.9 %g us, max %g us, this machine\n",
           p50 / 1e3, p99 / 1e3, p999 / 1e3, lat[n - 1] / 1e3);
    if (p50 > 0.0) printf("  p99.9 is %gx the median: the average would hide it\n", p999 / p50);
    printf("  honest note: these reads hit the page cache too (rule 2); a cold NVMe read is typically near 100 us\n");

    remove(path);
    free(data);
    free(lat);
    const int ok = written && file_bytes == expected && seen == expected;
    printf("\n%s%" PRIu64 " bytes\n", ok ? "every byte written was read back: " : "MISMATCH: expected ", expected);
    return ok ? 0 : 1;
}
