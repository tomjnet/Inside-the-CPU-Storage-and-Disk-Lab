/* Linux Page Cache: Why Disk I/O Often Never Touches the Disk Immediately - slide 8: mmap: a window into the page cache (C version of 08_mmap_cache.cpp) */
/* Build: make 08_mmap_cache_c */
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
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#define K_SIZE ((size_t)32 << 20)    /* 32 MiB = 8192 pages */

/* The answer both paths must give: one byte per page, added up. */
static uint64_t expected_sum(const char *data, size_t len) {
    uint64_t sum = 0;
    for (size_t i = 0; i < len; i += 4096) sum += (unsigned char)data[i];
    return sum;
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

static long minor_faults(void) {
    struct rusage u;
    memset(&u, 0, sizeof u);
    getrusage(RUSAGE_SELF, &u);
    return u.ru_minflt;
}
#endif

int main(void) {
    char *data = malloc(K_SIZE);
    if (data == NULL) { printf("out of memory\n"); return 1; }
    for (size_t i = 0; i < K_SIZE; ++i) data[i] = (char)(i * 31 + 7);
    const uint64_t expected = expected_sum(data, K_SIZE);
    printf("one byte from each of %zu pages, expected sum %" PRIu64 "\n", K_SIZE / 4096, expected);

#if defined(__linux__)
    char path[128];
    snprintf(path, sizeof path, "/tmp/pagecache_lab_08_%ld.bin", (long)getpid());
    int wfd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0600);
    const int written = wfd >= 0 && write_all(wfd, data, K_SIZE);   /* the pages are in the page cache now */
    const int write_errno = errno;
    if (wfd >= 0) close(wfd);
    free(data);                      /* the file holds the bytes now; C frees by hand */
    if (!written) {
        printf("could not create the file under /tmp: %s\n", strerror(write_errno));
        unlink(path);
        return 0;
    }

    /* map the file: the pointer names the page cache pages themselves */
    int fd = open(path, O_RDONLY);
    void *p = mmap(NULL, K_SIZE, PROT_READ, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) {
        printf("mmap refused: %s\n", strerror(errno));
        if (fd >= 0) close(fd);
        unlink(path);
        return 0;
    }
    const long faults_before = minor_faults();
    const volatile char *bytes = p;  /* volatile: the compiler must really touch each page */
    uint64_t sum = 0;
    for (size_t i = 0; i < K_SIZE; i += 4096)       /* one per page */
        sum += (unsigned char)bytes[i];             /* fault, no copy */
    const long faults = minor_faults() - faults_before;
    munmap(p, K_SIZE);
    close(fd);
    unlink(path);

    printf("sum through the mapping: %" PRIu64 "%s\n", sum, sum == expected ? "  (matches)" : "  (MISMATCH)");
    printf("minor page faults in the loop: %ld for %zu pages\n", faults, K_SIZE / 4096);
    printf("fewer faults than pages is normal: the kernel maps the cached neighbours of a page in the same fault\n");
    printf("no read call, no user buffer: the loop read the page cache pages in place\n");
    return sum == expected ? 0 : 1;
#else
    free(data);
    printf("this sample needs Linux: it maps a 32 MiB file with mmap, adds one byte per page through the\n"
           "pointer, and prints the minor page faults that connected the page cache pages to the process\n");
    return 0;
#endif
}
