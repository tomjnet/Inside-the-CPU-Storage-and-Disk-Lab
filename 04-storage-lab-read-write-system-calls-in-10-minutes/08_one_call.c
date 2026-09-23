/* Read and Write in C++: What Happens Behind the System Call - slide 8: lab: the same bytes in one call (C version of 08_one_call.cpp) */
/* Build: make 08_one_call_c */
/* Run:   ./08_one_call_c        timed: both versions, then the ratio */
/*        ./08_one_call_c once   one untimed write of 1 MiB, for strace */
#if defined(__linux__)
#define _GNU_SOURCE
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#if defined(__linux__)
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* ---- timing harness: the C twin of bench_ns ---- */
static double now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

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

/* slide 6: write may take fewer bytes than asked, so loop until all are in; 1 on success, 0 on error */
static int write_all(int fd, const char *p, size_t left) {
    while (left > 0) {
        ssize_t n = write(fd, p, left);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) return 0;
        p += n;
        left -= (size_t)n;
    }
    return 1;
}

static char buf[1 << 20];                          /* 1 MiB, static storage */

/* slide 7, the slow version: a function plus a context pointer (the path) replaces the lambda */
static void per_byte(void *ctx) {
    const char *path = ctx;
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { perror("open"); return; }
    for (size_t i = 0; i < sizeof buf; ++i)
        if (write(fd, &buf[i], 1) != 1) break;
    close(fd);
}

/* the same 1 MiB in one write: 1 system call, 1 copy of 1 MiB */
static void one_call(void *ctx) {
    const char *path = ctx;
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { perror("open"); return; }
    if (!write_all(fd, buf, sizeof buf)) perror("write");
    close(fd);
}
#endif

int main(int argc, char *argv[]) {
    const int once = argc > 1 && strcmp(argv[1], "once") == 0;
    if (!once)
        printf("same 1 MiB, same file, same page cache: 1,048,576 system calls against 1\n\n");

#if defined(__linux__)
    char path[] = "/tmp/storage_lab_08.bin";

    if (once) {                                    /* the pass strace counts: 1 write for the data, 1 for this line */
        int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) { perror("open"); return 1; }
        int ok = write_all(fd, buf, sizeof buf);
        close(fd);
        unlink(path);
        printf("once: 1 write call of %zu bytes: %s\n", sizeof buf, ok ? "ok" : "failed");
        return ok ? 0 : 1;
    }

    /* slide 7, the slow version, so the ratio below compares two runs of this program */
    BenchResult slow = bench_ns(per_byte, path, 1, 3);
    BenchResult fast = bench_ns(one_call, path, 3, 21);
    /* same bytes, same file, same page cache: only the call count differs */
    printf("one call : %.2f ms\n", fast.median_ns / 1e6);
    printf("per byte : %.2f ms\n", slow.median_ns / 1e6);
    printf("ratio    : %.0fx (this machine)\n",
           slow.median_ns / fast.median_ns);

    /* correctness, not timing, decides the exit code: the last run must have left exactly 1 MiB */
    struct stat st = {0};
    int size_ok = stat(path, &st) == 0 && (size_t)st.st_size == sizeof buf;
    printf("file size after the last run: %lld bytes (%s)\n", (long long)st.st_size,
           size_ok ? "all of it arrived" : "WRONG");
    unlink(path);
    return size_ok ? 0 : 1;
#else
    printf("this sample needs Linux: it times one write of 1 MiB against 1,048,576 writes of 1 byte\n"
           "and prints the ratio, typically several hundred to more than a thousand\n");
    return 0;
#endif
}
