/* Read and Write in C++: What Happens Behind the System Call - slide 9: lab: std::ofstream, one byte per put (C version of 09_ofstream_put.cpp) */
/* Build: make 09_ofstream_put_c */
/* Run:   ./09_ofstream_put_c        timed: fputc a byte at a time, then "\n" against "\n" plus fflush */
/*        ./09_ofstream_put_c once   one untimed pass, for strace (see 10_strace_count.sh) */
/* Portable: a stdio FILE buffers in user space on every platform, so this one runs on Windows too. */
/* C has no std::ofstream and no std::endl: FILE, fputc and fputs play the stream, fflush plays endl. */
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

static char buf[1 << 20];                          /* 1 MiB, static storage */
enum { LINES = 20000 };

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

/* the file size, or -1 when the file cannot be opened */
static long long file_size(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) return -1;
    long long size = -1;
    if (fseek(f, 0, SEEK_END) == 0) size = (long long)ftell(f);
    fclose(f);
    return size;
}

/* one byte per fputc, but fputc is a function call, not a system call */
static void put_bytes(void *ctx) {
    const char *path = ctx;
    FILE *out = fopen(path, "wb");
    if (out == NULL) { perror("fopen"); return; }
    for (size_t i = 0; i < sizeof buf; ++i)
        fputc(buf[i], out);      /* lands in the FILE's user space buffer */
    fclose(out);                 /* buffer full or close: one write(2) */
}

static void lines_newline(void *ctx) {
    FILE *out = fopen((const char *)ctx, "w");
    if (out == NULL) { perror("fopen"); return; }
    for (int i = 0; i < LINES; ++i) fputs("order accepted\n", out);
    fclose(out);
}

static void lines_flush(void *ctx) {
    FILE *out = fopen((const char *)ctx, "w");
    if (out == NULL) { perror("fopen"); return; }
    for (int i = 0; i < LINES; ++i) { fputs("order accepted\n", out); fflush(out); }   /* what std::endl does */
    fclose(out);
}

int main(int argc, char *argv[]) {
    const int once = argc > 1 && strcmp(argv[1], "once") == 0;
    char path[1024];
    temp_path(path, sizeof path, "storage_lab_09.bin");

    if (once) {                                    /* the pass strace counts: no timing loop around it */
        put_bytes(path);
        long long size = file_size(path);
        remove(path);
        printf("once: %zu put calls, %lld bytes in the file\n", sizeof buf, size);
        return size == (long long)sizeof buf ? 0 : 1;
    }

    BenchResult buffered = bench_ns(put_bytes, path, 3, 21);
    /* the stdio buffer is a few KiB (glibc sizes it from the file's block size, often 4 KiB):
       a few hundred write calls, not a million */
    printf("ofstream : %.2f ms\n", buffered.median_ns / 1e6);

    printf("per put  : %.1f ns (this machine): the price of a function call, not of a system call\n",
           buffered.median_ns / (double)sizeof buf);
    long long size = file_size(path);
    printf("file size: %lld bytes\n\n", size);

    /* the trap: std::endl is "\n" plus flush, so every line becomes its own system call */
    BenchResult newline = bench_ns(lines_newline, path, 1, 5);
    BenchResult with_endl = bench_ns(lines_flush, path, 1, 5);
    printf("%d log lines ending in '\\n'      : %.2f ms\n", LINES, newline.median_ns / 1e6);
    printf("%d log lines ending in std::endl : %.2f ms\n", LINES, with_endl.median_ns / 1e6);
    printf("ratio: %.0fx (this machine): endl flushes, one system call per line\n",
           with_endl.median_ns / newline.median_ns);

    remove(path);
    return size == (long long)sizeof buf ? 0 : 1;
}
