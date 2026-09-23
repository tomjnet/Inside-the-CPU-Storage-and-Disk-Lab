/* Read and Write in C++: What Happens Behind the System Call - slide 7: lab: one byte per system call (C version of 07_byte_at_a_time.cpp) */
/* Build: make 07_byte_at_a_time_c */
/* Run:   ./07_byte_at_a_time_c        timed: 1 warm up and 3 runs */
/*        ./07_byte_at_a_time_c once   one untimed pass, for strace (see 10_strace_count.sh) */
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
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#include <fcntl.h>
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

static char buf[1 << 20];                          /* 1 MiB, static storage */

/* the lambda of the C++ version: a function plus a context pointer (the path) */
static void byte_at_a_time(void *ctx) {
    const char *path = ctx;
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { perror("open"); return; }
    for (size_t i = 0; i < sizeof buf; ++i)
        if (write(fd, &buf[i], 1) != 1) break;     /* mode switch each */
    close(fd);
}
#endif

int main(int argc, char *argv[]) {
    const int once = argc > 1 && strcmp(argv[1], "once") == 0;
    const double n_calls = 1048576.0;
    if (!once)
        printf("1 MiB at one byte per call is %.0f system calls; at a typical 100 to 300 ns for the mode\n"
               "switch alone that is %.2f to %.2f s of overhead before write does any work\n\n",
               n_calls, n_calls * 100e-9, n_calls * 300e-9);

#if defined(__linux__)
    char path[] = "/tmp/storage_lab_07.bin";

    if (once) {                                    /* the pass strace counts: no timing loop around it */
        int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) { perror("open"); return 1; }
        size_t done = 0;
        while (done < sizeof buf && write(fd, &buf[done], 1) == 1) ++done;
        close(fd);
        unlink(path);
        printf("once: %zu write calls of 1 byte\n", done);
        return done == sizeof buf ? 0 : 1;
    }

    /* 1 MiB, one byte per write: 1,048,576 system calls */
    BenchResult slow = bench_ns(byte_at_a_time, path, 1, 3);   /* 1 warm up, 3 runs */
    /* every call: enter the kernel, find the fd, copy 1 byte, return */
    double calls = (double)sizeof buf;
    printf("per call: %.0f ns\n", slow.median_ns / calls);

    printf("whole MiB: min %.1f ms, median %.1f ms (this machine)\n", slow.min_ns / 1e6, slow.median_ns / 1e6);
    printf("a virtual machine or WSL adds a layer to every kernel entry: take the number on real Linux hardware\n");
    unlink(path);
    return 0;
#else
    printf("this sample needs Linux: it times 1,048,576 write(fd, &buf[i], 1) calls and prints the cost per call\n");
    return 0;
#endif
}
