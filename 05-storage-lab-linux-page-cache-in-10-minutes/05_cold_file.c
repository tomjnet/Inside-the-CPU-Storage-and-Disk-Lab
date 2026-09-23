/* Linux Page Cache: Why Disk I/O Often Never Touches the Disk Immediately - slide 5: a file that is really cold (C version of 05_cold_file.cpp) */
/* Build: make 05_cold_file_c */
#if defined(__linux__)
#define _GNU_SOURCE                  /* sched_setaffinity, CPU_SET, O_DIRECT, clock_gettime */
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS      /* fopen, strerror: MSVC deprecates the standard names */
#endif

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(__linux__)
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#define K_SIZE ((size_t)32 << 20)    /* 32 MiB */
#define K_PAGE ((size_t)4096)        /* the unit of the page cache */

#if defined(__linux__)
/* The short write loop of the previous episode: write may accept fewer bytes than asked. */
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

/* a file that is on the device and NOT in the page cache */
static int make_cold_file(const char *path, const char *d, size_t len) {
    int fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0600);
    if (fd < 0) return 0;
    int ok = write_all(fd, d, len);   /* 32 MiB: 8192 dirty pages */
    ok = ok && fsync(fd) == 0;        /* flushed: the pages are clean */
    /* clean pages can be dropped: one file, no root needed */
    ok = ok && posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED) == 0;
    close(fd);
    return ok;
}

/* How many pages of the file are in the page cache right now: mincore answers for a mapping of the file. */
static long cached_pages(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    void *p = mmap(NULL, K_SIZE, PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    if (p == MAP_FAILED) return -1;
    unsigned char *resident = malloc(K_SIZE / K_PAGE);
    long count = -1;
    if (resident != NULL && mincore(p, K_SIZE, resident) == 0) {
        count = 0;
        for (size_t i = 0; i < K_SIZE / K_PAGE; ++i) count += resident[i] & 1;
    }
    int saved = errno;
    free(resident);
    munmap(p, K_SIZE);
    errno = saved;
    return count;
}

static void read_whole_file(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return;
    char *buf = malloc((size_t)1 << 20);
    if (buf != NULL)
        while (read(fd, buf, (size_t)1 << 20) > 0) {}
    free(buf);
    close(fd);
}
#endif

int main(void) {
    char *data = malloc(K_SIZE);     /* C has no std::vector: malloc plus a length, freed on every path */
    if (data == NULL) { printf("out of memory\n"); return 1; }
    for (size_t i = 0; i < K_SIZE; ++i) data[i] = (char)(i * 31 + 7);
    printf("file size: %zu MiB = %zu pages of 4 KiB\n", K_SIZE >> 20, K_SIZE / K_PAGE);

#if defined(__linux__)
    char path[128];
    snprintf(path, sizeof path, "/tmp/pagecache_lab_05_%ld.bin", (long)getpid());

    if (!make_cold_file(path, data, K_SIZE)) {
        printf("could not create the cold file: %s\n", strerror(errno));
        unlink(path);
        free(data);
        return 0;
    }
    const long after_drop = cached_pages(path);
    const int drop_errno = errno;
    read_whole_file(path);
    const long after_read = cached_pages(path);
    unlink(path);

    printf("pages of the file in the page cache\n");
    printf("  after write, fsync and POSIX_FADV_DONTNEED: %ld\n", after_drop);
    printf("  after one read of the whole file:           %ld\n", after_read);
    if (after_drop > 0)
        printf("this filesystem kept some pages: tmpfs lives in the page cache, and the advice is only advice\n");
    else if (after_drop == 0)
        printf("the file was cold: the next read has to go to the device\n");
    else
        printf("mincore is not available here: %s\n", strerror(drop_errno));
#else
    printf("this sample needs Linux: it writes the file, calls fsync and posix_fadvise(POSIX_FADV_DONTNEED),\n"
           "then counts the cached pages with mincore: 0 after the advice, 8192 after one read\n");
#endif
    free(data);
    return 0;
}
