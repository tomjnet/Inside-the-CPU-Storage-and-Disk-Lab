/* Buffered I/O vs Direct I/O: How Low Latency Storage Works - slide 7: buffered against direct: time one write (C version of 07_buffered_vs_direct.cpp) */
/* Build: make 07_buffered_vs_direct_c */
#if defined(__linux__)
#define _GNU_SOURCE                  /* sched_setaffinity, CPU_SET, O_DIRECT, clock_gettime */
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS      /* fopen, strerror: MSVC deprecates the standard names */
#endif

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

static void row(const char *name, BenchResult r) {
    printf("  %-34s%12.0f ns min%12.0f ns median\n", name, r.min_ns, r.median_ns);
}

#define K_BLOCK 4096

/* Portable floor: a buffered write is at least one copy of the block into the page cache. */
static char src[K_BLOCK];
static char dst[K_BLOCK];
static void copy_block(void *ctx) {
    (void)ctx;
    memcpy(dst, src, K_BLOCK);
    sink((uint64_t)dst[K_BLOCK / 2]);
}

#if defined(__linux__)
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>

/* A temporary file under /tmp, buffered or O_DIRECT. C has no destructor: tmpfile_close() on every way out. */
typedef struct {
    char path[128];
    int fd;
    int direct;
} TmpFile;

static void tmpfile_open(TmpFile *t, const char *tag, int want_direct) {
    snprintf(t->path, sizeof t->path, "/tmp/storage-lab-08-%s-%ld", tag, (long)getpid());
    t->fd = -1;
    t->direct = 0;
    if (want_direct) {
        t->fd = open(t->path, O_RDWR | O_CREAT | O_DIRECT, 0600);
        t->direct = t->fd >= 0;
        if (t->direct) {   /* some filesystems accept the flag at open and refuse the first transfer: try one */
            void *probe = NULL;
            if (posix_memalign(&probe, 4096, 4096) == 0) {
                memset(probe, 0, 4096);
                if (pwrite(t->fd, probe, 4096, 0) != 4096) t->direct = 0;
                free(probe);
            }
            if (!t->direct) close(t->fd);
        }
        if (!t->direct) printf("O_DIRECT refused here (%s): the direct row is buffered too\n", strerror(errno));
    }
    if (!t->direct) t->fd = open(t->path, O_RDWR | O_CREAT, 0600);
}

static void tmpfile_close(TmpFile *t) {
    if (t->fd >= 0) close(t->fd);
    unlink(t->path);
}

typedef struct { int fd; const void *buf; } WriteCtx;

/* buffered: write() is a copy into the page cache, the device waits */
static void one_pwrite(void *ctx) {
    const WriteCtx *c = ctx;
    sink((uint64_t)pwrite(c->fd, c->buf, K_BLOCK, 0));
}

/* buffered plus fsync: now the device is inside the measurement */
static void pwrite_fsync(void *ctx) {
    const WriteCtx *c = ctx;
    sink((uint64_t)pwrite(c->fd, c->buf, K_BLOCK, 0));
    sink((uint64_t)fsync(c->fd));
}
#endif

int main(void) {
    memset(src, 'B', K_BLOCK);
    BenchResult copy = bench_ns(copy_block, NULL, 3, 21);
    printf("one 4 KiB block, this machine (warm up 3, 21 repeats):\n");
    row("memcpy of the block (the floor)", copy);

#if defined(__linux__)
    TmpFile buffered_file, direct_file;
    tmpfile_open(&buffered_file, "buffered", 0);
    tmpfile_open(&direct_file, "direct", 1);
    if (buffered_file.fd < 0 || direct_file.fd < 0) {
        printf("cannot create the files under /tmp: %s\n", strerror(errno));
        tmpfile_close(&buffered_file);
        tmpfile_close(&direct_file);
        return 1;
    }
    void *buf = NULL;
    if (posix_memalign(&buf, K_BLOCK, K_BLOCK) != 0) {
        tmpfile_close(&buffered_file);
        tmpfile_close(&direct_file);
        return 1;
    }
    memset(buf, 'D', K_BLOCK);

    WriteCtx cb = { buffered_file.fd, buf };
    WriteCtx cd = { direct_file.fd, buf };
    BenchResult buffered = bench_ns(one_pwrite, &cb, 3, 21);
    BenchResult synced = bench_ns(pwrite_fsync, &cb, 3, 21);
    /* direct: every call is one device write, no dirty page left behind */
    BenchResult direct = bench_ns(one_pwrite, &cd, 3, 21);

    free(buf);
    row("buffered pwrite", buffered);
    row("buffered pwrite + fsync", synced);
    row(direct_file.direct ? "direct pwrite (O_DIRECT)" : "direct pwrite (fell back: buffered)", direct);
    printf("  direct / buffered median        : %.1fx (this machine)\n", direct.median_ns / buffered.median_ns);
    printf("  fsync  / direct median          : %.1fx (this machine)\n", synced.median_ns / direct.median_ns);
    printf("direct I/O is not faster than a copy into RAM: it puts the device into every call, so there is\n"
           "no writeback later and the median sits close to the tail. A virtual disk (WSL, a cloud volume)\n"
           "shows other numbers than an NVMe drive on real hardware: read your own.\n");
    tmpfile_close(&buffered_file);
    tmpfile_close(&direct_file);
#else
    printf("this sample needs Linux: it times pwrite on a buffered file, pwrite plus fsync, and pwrite on an\n"
           "O_DIRECT file; typical on NVMe: about a microsecond, then tens to hundreds of microseconds\n");
#endif
    return 0;
}
