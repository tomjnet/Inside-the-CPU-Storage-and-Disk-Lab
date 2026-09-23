/* Linux Page Cache: Why Disk I/O Often Never Touches the Disk Immediately - slide 6: first read against second read (C version of 06_read_twice.cpp) */
/* Build: make 06_read_twice_c */
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

#if defined(__linux__)
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#endif

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

#define K_SIZE ((size_t)32 << 20)    /* 32 MiB */
#define K_BUF ((size_t)1 << 20)      /* 1 MiB per read call */

/* A plain stopwatch, not the repeating harness: here the second run IS the experiment.
   C has no lambda: a function pointer plus a context pointer. */
static double ms(void (*f)(void *), void *ctx) {
    double t0 = now_ns();
    f(ctx);
    return (now_ns() - t0) / 1e6;
}

typedef struct { const char *path; char *buf; } ReadCtx;

#if defined(__linux__)
static int write_all(int fd, const char *p, size_t len) {
    size_t done = 0;
    while (done < len) {
        ssize_t n = write(fd, p + done, len - done);
        if (n < 0) {
            if (errno == EINTR) continue;
            return 0;
        }
        done += (size_t)n;
    }
    return 1;
}

/* slide 5: on the device and not in the page cache */
static int make_cold_file(const char *path, const char *d, size_t len) {
    int fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0600);
    if (fd < 0) return 0;
    int ok = write_all(fd, d, len);
    ok = ok && fsync(fd) == 0;
    ok = ok && posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED) == 0;
    close(fd);
    return ok;
}

/* the whole file with 1 MiB read calls: 32 system calls */
static void read_all(void *ctx) {
    const ReadCtx *c = ctx;
    int fd = open(c->path, O_RDONLY);
    if (fd < 0) return;
    while (read(fd, c->buf, K_BUF) > 0) {}  /* copy to buf */
    close(fd);
}
#else
/* The portable version of the same loop: fread ends in the read call of the operating system too. */
static void read_all_portable(void *ctx) {
    const ReadCtx *c = ctx;
    FILE *in = fopen(c->path, "rb");
    if (in == NULL) return;
    while (fread(c->buf, 1, K_BUF, in) > 0) {}
    fclose(in);
}
#endif

static void report(double cold, double warm) {
    const double mib = (double)(K_SIZE >> 20);
    printf("  first read:  %8.2f ms  %9.2f MiB/s\n", cold, mib / (cold / 1000.0));
    printf("  second read: %8.2f ms  %9.2f MiB/s\n", warm, mib / (warm / 1000.0));
    printf("  ratio on this machine: %.2fx\n", cold / warm);
}

int main(void) {
    char *data = malloc(K_SIZE);
    char *buf = malloc(K_BUF);
    if (data == NULL || buf == NULL) {
        printf("out of memory\n");
        free(data);
        free(buf);
        return 1;
    }
    for (size_t i = 0; i < K_SIZE; ++i) data[i] = (char)(i * 31 + 7);

#if defined(__linux__)
    char path[128];
    snprintf(path, sizeof path, "/tmp/pagecache_lab_06_%ld.bin", (long)getpid());
    if (!make_cold_file(path, data, K_SIZE)) {
        printf("could not create the cold file: %s\n", strerror(errno));
        unlink(path);
        free(data);
        free(buf);
        return 0;
    }

    /* first: misses, the device works; second: hits, a memcpy */
    ReadCtx ctx = { path, buf };
    double cold = ms(read_all, &ctx);
    double warm = ms(read_all, &ctx);
    unlink(path);

    printf("32 MiB read twice with 32 read calls of 1 MiB (this machine):\n");
    report(cold, warm);
    printf("same code, same system calls: the second time the pages were in the page cache\n");
    printf("inside a virtual machine or WSL the gap can shrink: the host caches the virtual disk too\n");
#else
    const char *tmpdir = getenv("TEMP");
    if (tmpdir == NULL || tmpdir[0] == '\0') tmpdir = ".";
    char path[512];
    snprintf(path, sizeof path, "%s/pagecache_lab_06.bin", tmpdir);
    FILE *out = fopen(path, "wb");
    if (out == NULL) {
        printf("could not create %s\n", path);
        free(data);
        free(buf);
        return 0;
    }
    fwrite(data, 1, K_SIZE, out);
    fclose(out);
    ReadCtx ctx = { path, buf };
    double cold = ms(read_all_portable, &ctx);
    double warm = ms(read_all_portable, &ctx);
    remove(path);

    printf("32 MiB read twice with fread (this machine):\n");
    report(cold, warm);
    printf("this is not Linux: both reads are probably hits, because the file was just written and portable\n"
           "C cannot evict it. The Linux build drops it with posix_fadvise and shows a real miss.\n");
#endif
    free(data);
    free(buf);
    return 0;
}
