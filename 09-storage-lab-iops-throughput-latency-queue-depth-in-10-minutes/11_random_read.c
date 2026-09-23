/* Storage Performance: IOPS, Throughput, Latency and Queue Depth - slide 11: random reads in c++ (C version of 11_random_read.cpp) */
/* Build: make 11_random_read_c */
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

#if defined(__linux__)
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#endif

/* ---- timing: now_ns and sink from the timing harness ---- */
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
static void sink(uint64_t x) { g_sink = g_sink + x; }   /* keeps the result alive: the loop cannot be deleted */

static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

#define K_BLOCKS 4096u                                   /* 4096 blocks of 4 KiB: a 16 MiB test file */

/* fixed-seed xorshift64 instead of std::mt19937: same offsets every run (not the C++ offsets) */
static uint64_t xorshift64(uint64_t *s) {
    uint64_t x = *s;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    return *s = x;
}

static double percentile(const double *sorted, size_t n, double p) {
    double rank = p / 100.0 * (double)(n - 1);
    return sorted[(size_t)rank];
}

static void report(const char *label, double *lat_ns, size_t n) {
    qsort(lat_ns, n, sizeof lat_ns[0], cmp_double);
    double sum = 0.0;
    for (size_t i = 0; i < n; ++i) sum += lat_ns[i];
    double mean = sum / (double)n;
    printf("%s (this machine, %u reads of 4 KiB at queue depth 1)\n", label, (unsigned)n);
    printf("  p50    %.1f us\n", percentile(lat_ns, n, 50.0) / 1000.0);
    printf("  p99    %.1f us\n", percentile(lat_ns, n, 99.0) / 1000.0);
    printf("  p99.9  %.1f us\n", percentile(lat_ns, n, 99.9) / 1000.0);
    printf("  max    %.1f us\n", lat_ns[n - 1] / 1000.0);
    printf("  mean   %.1f us, so %.1f IOPS (1 s / mean latency)\n", mean / 1000.0, 1e9 / mean);
}

/* both versions fill lat_ns (n_reads entries, owned by the caller: C has no std::vector) */
#if defined(__linux__)
static void random_reads(int fd, void *buf, double *lat_ns, int n_reads) {
    /* 4 KiB random reads: one pread = one system call = one I/O */
    uint64_t rng = 42;                           /* same offsets every run */
    for (int i = 0; i < n_reads; ++i) {
        off_t off = (off_t)(xorshift64(&rng) % K_BLOCKS) * 4096;   /* block aligned */
        double t0 = now_ns();
        ssize_t n = pread(fd, buf, 4096, off);   /* queue depth 1 */
        double t1 = now_ns();
        sink((uint64_t)n);                       /* keep the result alive */
        lat_ns[i] = t1 - t0;                     /* no copy of the data */
    }
}
#else
static void random_reads(FILE *in, char *buf, double *lat_ns, int n_reads) {
    /* the same loop with the portable stream: fseek plus fread instead of one pread */
    uint64_t rng = 42;
    for (int i = 0; i < n_reads; ++i) {
        long off = (long)(xorshift64(&rng) % K_BLOCKS) * 4096L;
        double t0 = now_ns();
        fseek(in, off, SEEK_SET);
        size_t got = fread(buf, 1, 4096, in);
        double t1 = now_ns();
        sink((uint64_t)got);
        lat_ns[i] = t1 - t0;
    }
}
#endif

int main(void) {
#if defined(__linux__)
    const char *dir = "/tmp";
#else
    const char *dir = getenv("TEMP");
    if (dir == NULL) dir = ".";
#endif
    char path[512];
    snprintf(path, sizeof path, "%s/tomjnet_storage_lab_09_%" PRIu64 ".dat", dir, (uint64_t)now_ns());

    {   /* the test file: 16 MiB of a repeating pattern */
        char block[4096];
        for (size_t i = 0; i < sizeof block; ++i) block[i] = (char)('a' + i % 26);
        FILE *out = fopen(path, "wb");
        int ok = out != NULL;
        for (unsigned b = 0; ok && b < K_BLOCKS; ++b) ok = fwrite(block, 1, sizeof block, out) == sizeof block;
        if (out != NULL && fclose(out) != 0) ok = 0;
        if (!ok) {
            printf("cannot write %s: nothing to measure here\n", path);
            remove(path);
            return 0;
        }
    }
    printf("test file: %s (%u MiB)\n\n", path, (unsigned)(K_BLOCKS * 4096u / (1024u * 1024u)));
    int status = 0;

#if defined(__linux__)
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        printf("open failed: %s\n", strerror(errno));
    } else {
        fsync(fd);                                           /* the data reaches the device before the direct pass */
        static char page_buf[4096];
        for (unsigned b = 0; b < K_BLOCKS; ++b) {            /* warm up: every block is in the page cache now */
            ssize_t n = pread(fd, page_buf, 4096, (off_t)b * 4096);
            if (n != 4096) status = 1;                       /* a short read of our own file is a real error */
        }
        double *buffered = malloc(100000 * sizeof *buffered);
        if (buffered == NULL) {
            printf("out of memory\n");
            status = 1;
        } else {
            random_reads(fd, page_buf, buffered, 100000);
            report("buffered: page cache hits, a system call plus a copy", buffered, 100000);
            free(buffered);
        }
        close(fd);

        /* the same loop with O_DIRECT: every read goes to the device (episode 8: aligned buffer, size and offset) */
        int dfd = open(path, O_RDONLY | O_DIRECT);
        void *aligned = NULL;
        if (dfd < 0) {
            printf("\nO_DIRECT refused here: %s\n"
                   "on a real filesystem over NVMe you would see about 100 us per read, often 50 to 100\n"
                   "times the buffered median\n", strerror(errno));
        } else if (posix_memalign(&aligned, 4096, 4096) != 0) {
            aligned = NULL;
            printf("\nposix_memalign failed: no direct pass\n");
        } else {
            double *direct = malloc(5000 * sizeof *direct);
            if (direct == NULL) {
                printf("out of memory\n");
                status = 1;
            } else {
                random_reads(dfd, aligned, direct, 5000);
                printf("\n");
                report("direct: O_DIRECT, the page cache is bypassed", direct, 5000);
                printf("\nbuffered measures RAM, direct measures the device (in a VM or in WSL: a virtual disk).\n"
                       "Take the timings on real Linux hardware.\n");
                free(direct);
            }
        }
        free(aligned);                                       /* no destructor in C: free and close by hand */
        if (dfd >= 0) close(dfd);
    }
#else
    printf("this sample needs Linux: pread, then O_DIRECT to reach the device instead of the page cache.\n"
           "Here the same random reads go through fopen and fread, so they measure the file cache of the OS.\n\n");
    {
        FILE *in = fopen(path, "rb");
        static char page_buf[4096];
        double *buffered = malloc(100000 * sizeof *buffered);
        if (in == NULL || buffered == NULL) {
            printf("cannot open the test file or allocate the samples\n");
            status = 1;
        } else {
            for (unsigned b = 0; b < K_BLOCKS; ++b)          /* warm up: the whole file once */
                if (fread(page_buf, 1, 4096, in) != 4096) status = 1;   /* a short read of our own file is a real error */
            random_reads(in, page_buf, buffered, 100000);
            report("buffered: fopen, fseek plus fread", buffered, 100000);
        }
        free(buffered);
        if (in != NULL) fclose(in);                          /* close before remove: Windows keeps open files */
    }
#endif

    int rm = remove(path);                                   /* the temporary file never stays behind */
    printf("\nremoved the test file%s, g_sink = %" PRIu64 "\n", rm != 0 ? " (failed)" : "", (uint64_t)g_sink);
    return status;
}
