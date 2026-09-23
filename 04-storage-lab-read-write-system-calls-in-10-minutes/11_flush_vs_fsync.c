/* Read and Write in C++: What Happens Behind the System Call - slide 11: flush is not fsync (C version of 11_flush_vs_fsync.cpp) */
/* Build: make 11_flush_vs_fsync_c */
/* C has no std::ofstream: a stdio FILE is the user space buffer, fflush is flush, fsync stays the same */
#if defined(__linux__)
#define _GNU_SOURCE
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__linux__)
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#endif

/* ---- timing harness: the C twin of bench_ns ---- */
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

/* the system temp folder: /tmp on Linux, %TEMP% on Windows, the current folder as fallback */
static void temp_path(char *out, size_t size, const char *name) {
#if defined(_WIN32)
    const char *dir = getenv("TEMP");
    const char sep = '\\';
#else
    const char *dir = "/tmp";
    const char sep = '/';
#endif
    if (dir == NULL || dir[0] == '\0') dir = ".";
    snprintf(out, size, "%s%c%s", dir, sep, name);
}

static int show(const char *path) {                /* what another process would see in the file right now */
    FILE *in = fopen(path, "r");
    if (in == NULL) return 0;
    char line[256];
    int lines = 0;
    while (fgets(line, sizeof line, in) != NULL) {
        line[strcspn(line, "\n")] = '\0';
        printf("  file: %s\n", line);
        ++lines;
    }
    fclose(in);
    return lines;
}

static void do_flush(void *ctx) { fflush((FILE *)ctx); }

#if defined(__linux__)
static void do_fsync(void *ctx) { if (fsync(*(int *)ctx)) perror("fsync"); }
#endif

int main(void) {
    char path[1024];
    temp_path(path, sizeof path, "storage_lab_11.log");
    int expected = 1;

    /* portable part: before flush the line lives only in the stream buffer, after flush it is in the file */
    FILE *out = fopen(path, "w");
    if (out == NULL) { perror("fopen"); return 1; }
    fputs("order 41 accepted\n", out);
    printf("before flush, the file holds %d lines: the bytes are still in the stream buffer\n", show(path));
    BenchResult t_flush = bench_ns(do_flush, out, 0, 1);
    printf("flush: %.3f ms (this machine): one system call into the page cache, no device wait\n",
           t_flush.median_ns / 1e6);
    printf("after flush, the file holds:\n");
    show(path);
    fclose(out);                                   /* no destructor in C: the close is ours */

#if defined(__linux__)
    printf("\nnow the slide: flush, then write and fsync on a file descriptor\n");
    FILE *log = fopen(path, "w");
    if (log == NULL) { perror("fopen"); remove(path); return 1; }
    fputs("order 42 accepted\n", log);
    fflush(log);   /* user buffer to page cache: survives a crash of
                      this process, not a power cut */
    int fd = open(path, O_WRONLY | O_APPEND);
    if (fd < 0) {
        perror("open");
    } else {
        if (write(fd, "order 43 accepted\n", 18) != 18) perror("write");
        BenchResult t = bench_ns(do_fsync, &fd, 0, 1);
        printf("fsync: %.2f ms\n", t.median_ns / 1e6);  /* device wait */
        close(fd);
    }

    fclose(log);
    expected = 2;
    printf("(this machine; typical: 0.1 ms on NVMe, 10 ms or more on a hard disk, close to zero when\n"
           " the temp folder is a tmpfs, because RAM has no device to wait for)\n");
#else
    printf("\nthis sample needs Linux for the second half: write on a file descriptor, then fsync(fd),\n"
           "which returns only when the device confirms (typically 0.1 to 10 ms)\n");
#endif

    int lines = show(path);
    remove(path);
    return lines == expected ? 0 : 1;
}
