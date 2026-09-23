/* Linux Page Cache: Why Disk I/O Often Never Touches the Disk Immediately - slide 7: write against write plus fsync (C version of 07_write_fsync.cpp) */
/* Build: make 07_write_fsync_c */
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

/* C has no lambda: a function pointer plus a context pointer */
static double ms(void (*f)(void *), void *ctx) {
    double t0 = now_ns();
    f(ctx);
    return (now_ns() - t0) / 1e6;
}

static void line(const char *label, double millis) {
    const double mib = (double)(K_SIZE >> 20);
    printf("  %s%9.2f ms  %9.2f MiB/s\n", label, millis, mib / (millis / 1000.0));
}

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

enum { SYNC_NONE, SYNC_FSYNC, SYNC_FDATASYNC };
typedef struct { int fd; const char *data; int sync; } WriteCtx;

static void write_then_sync(void *ctx) {
    const WriteCtx *c = ctx;
    (void)write_all(c->fd, c->data, K_SIZE);
    if (c->sync == SYNC_FSYNC) (void)fsync(c->fd);             /* waits for the device */
    if (c->sync == SYNC_FDATASYNC) (void)fdatasync(c->fd);     /* the same, minus metadata */
}
#else
typedef struct { const char *path; const char *data; } WriteCtx;

static void write_and_flush(void *ctx) {
    const WriteCtx *c = ctx;
    FILE *out = fopen(c->path, "wb");
    if (out == NULL) return;
    fwrite(c->data, 1, K_SIZE, out);
    fflush(out);   /* empties the buffer of the stream into the cache of the operating system: NOT an fsync */
    fclose(out);
}
#endif

int main(void) {
    char *data = malloc(K_SIZE);
    if (data == NULL) { printf("out of memory\n"); return 1; }
    for (size_t i = 0; i < K_SIZE; ++i) data[i] = (char)(i * 31 + 7);

#if defined(__linux__)
    char f1[128], f2[128], f3[128];
    snprintf(f1, sizeof f1, "/tmp/pagecache_lab_07_%ld_a.bin", (long)getpid());
    snprintf(f2, sizeof f2, "/tmp/pagecache_lab_07_%ld_b.bin", (long)getpid());
    snprintf(f3, sizeof f3, "/tmp/pagecache_lab_07_%ld_c.bin", (long)getpid());
    int fd = open(f1, O_CREAT | O_TRUNC | O_WRONLY, 0600);
    int fd2 = open(f2, O_CREAT | O_TRUNC | O_WRONLY, 0600);
    int fd3 = open(f3, O_CREAT | O_TRUNC | O_WRONLY, 0600);
    if (fd < 0 || fd2 < 0 || fd3 < 0) {
        printf("could not create the files under /tmp: %s\n", strerror(errno));
    } else {
        /* write returns when the bytes are in the page cache, not on disk */
        WriteCtx a = { fd, data, SYNC_NONE };         /* 32 MiB at memcpy speed */
        double cached = ms(write_then_sync, &a);
        /* fsync returns when the device says the data is durable */
        WriteCtx b = { fd2, data, SYNC_FSYNC };       /* the same 8192 pages */
        double durable = ms(write_then_sync, &b);
        /* fdatasync: the same, minus metadata such as the modification time */
        WriteCtx c = { fd3, data, SYNC_FDATASYNC };
        double data_only = ms(write_then_sync, &c);

        printf("32 MiB written three ways (this machine):\n");
        line("write alone:          ", cached);
        line("write plus fsync:     ", durable);
        line("write plus fdatasync: ", data_only);
        printf("  fsync against write alone on this machine: %.2fx\n", durable / cached);
        printf("the first line is the speed of a copy into RAM: it says nothing about the disk\n");
        printf("on tmpfs or under a hypervisor that ignores flushes the three lines come out close\n");
    }
    if (fd >= 0) close(fd);
    if (fd2 >= 0) close(fd2);
    if (fd3 >= 0) close(fd3);
    unlink(f1);
    unlink(f2);
    unlink(f3);
#else
    const char *tmpdir = getenv("TEMP");
    if (tmpdir == NULL || tmpdir[0] == '\0') tmpdir = ".";
    char path[512];
    snprintf(path, sizeof path, "%s/pagecache_lab_07.bin", tmpdir);
    WriteCtx ctx = { path, data };
    double buffered = ms(write_and_flush, &ctx);
    remove(path);

    printf("32 MiB written with fwrite (this machine):\n");
    line("write and flush:      ", buffered);
    printf("this is not Linux: fflush hands the bytes to the operating system cache and stops there.\n"
           "Portable C has no fsync; the Linux build times write alone against write plus fsync and fdatasync.\n");
#endif
    free(data);
    return 0;
}
