/* Buffered I/O vs Direct I/O: How Low Latency Storage Works - slide 5: open with o_direct, and expect a no (C version of 05_open_direct.cpp) */
/* Build: make 05_open_direct_c */
#if defined(__linux__)
#define _GNU_SOURCE                  /* sched_setaffinity, CPU_SET, O_DIRECT, clock_gettime */
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS      /* fopen, strerror: MSVC deprecates the standard names */
#endif

#include <stdio.h>

#if defined(__linux__)
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>

/* The same question for any directory: does the filesystem behind it accept O_DIRECT? */
static void probe(const char *dir) {
    char file[256];
    snprintf(file, sizeof file, "%s/storage-lab-08-probe-%ld.dat", dir, (long)getpid());
    int fd = open(file, O_RDWR | O_CREAT | O_DIRECT, 0600);
    if (fd >= 0) {
        printf("  %s: O_DIRECT accepted at open\n", dir);
        close(fd);
    } else {
        printf("  %s: refused, errno %d (%s)\n", dir, errno, strerror(errno));
    }
    unlink(file);
}
#endif

int main(void) {
    printf("what each path skips:\n"
           "  buffered I/O : nothing (page cache, kernel, device)\n"
           "  direct I/O   : the page cache (kernel, device)\n"
           "  SPDK         : the kernel (NVMe queues polled from user space, device)\n\n");

#if defined(__linux__)
    char path[128];
    snprintf(path, sizeof path, "/tmp/storage-lab-08-open-%ld.dat", (long)getpid());

    /* O_DIRECT: skip the page cache, the device does DMA on our buffer */
    int fd = open(path, O_RDWR | O_CREAT | O_DIRECT, 0600);
    int direct = fd >= 0;
    if (fd < 0 && errno == EINVAL) {
        /* tmpfs on older kernels, some network filesystems: refused */
        printf("O_DIRECT refused: %s\n", strerror(errno));
        fd = open(path, O_RDWR | O_CREAT, 0600);   /* fall back: buffered */
    }
    /* cost per call: 1 system call, 0 copies into the page cache */

    if (fd < 0) {
        printf("open failed: %s\n", strerror(errno));
        return 1;
    }
    const int flags = fcntl(fd, F_GETFL);
    printf("%s\n", path);
    printf("  opened with O_DIRECT : %s\n", direct ? "yes" : "no, fell back to buffered");
    printf("  O_DIRECT in F_GETFL  : %s\n\n", (flags & O_DIRECT) != 0 ? "set" : "not set");
    close(fd);
    unlink(path);

    printf("the same question for other filesystems of this machine:\n");
    probe("/tmp");
    probe("/dev/shm");   /* tmpfs: refused with EINVAL before Linux 6.6, accepted since */
    printf("a refusal can also arrive later, at the first read or write: check every return value\n");
#else
    printf("this sample needs Linux: open() with O_DIRECT, the EINVAL answer of a filesystem\n"
           "that refuses direct I/O, and the fall back to a buffered file descriptor\n");
#endif
    return 0;
}
