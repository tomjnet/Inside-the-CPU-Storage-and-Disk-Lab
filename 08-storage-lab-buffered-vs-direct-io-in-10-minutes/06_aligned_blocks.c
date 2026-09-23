/* Buffered I/O vs Direct I/O: How Low Latency Storage Works - slide 6: aligned buffers, 4 kib blocks (C version of 06_aligned_blocks.cpp) */
/* Build: make 06_aligned_blocks_c */
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

/* The arithmetic of the contract is portable: a value is aligned when the remainder is zero. */
static int aligned(uint64_t value, uint64_t block) { return value % block == 0; }

#if defined(__linux__)
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>

/* A temporary file under /tmp. C has no destructor: tmpfile_close() runs on every way out of main(). */
typedef struct {
    char path[128];
    int fd;
    int direct;
} TmpFile;

static void tmpfile_open(TmpFile *t, const char *tag) {
    snprintf(t->path, sizeof t->path, "/tmp/storage-lab-08-%s-%ld", tag, (long)getpid());
    t->fd = open(t->path, O_RDWR | O_CREAT | O_DIRECT, 0600);
    t->direct = t->fd >= 0;
    if (t->direct) {   /* some filesystems accept the flag at open and refuse the first transfer: try one */
        void *probe = NULL;
        if (posix_memalign(&probe, 4096, 4096) == 0) {
            memset(probe, 0, 4096);
            if (pwrite(t->fd, probe, 4096, 0) != 4096) t->direct = 0;
            free(probe);
        }
        if (!t->direct) close(t->fd);
    }
    if (!t->direct) {
        printf("O_DIRECT refused here (%s): falling back to buffered\n", strerror(errno));
        t->fd = open(t->path, O_RDWR | O_CREAT, 0600);
    }
}

static void tmpfile_close(TmpFile *t) {
    if (t->fd >= 0) close(t->fd);
    unlink(t->path);
}
#endif

int main(void) {
    printf("the contract of O_DIRECT, as arithmetic (block = 4096):\n");
    const uint64_t offsets[] = {0, 4096, 6000, 8192, 12288 + 512};
    for (size_t i = 0; i < sizeof offsets / sizeof offsets[0]; ++i) {
        const uint64_t off = offsets[i];
        printf("  offset %" PRIu64 "%s, block index %" PRIu64 ", byte %" PRIu64 " inside it\n", off,
               aligned(off, 4096) ? " : aligned" : " : NOT a multiple of 4096", off / 4096, off % 4096);
    }
    printf("\n");

#if defined(__linux__)
    TmpFile file;
    tmpfile_open(&file, "blocks");
    if (file.fd < 0) {
        printf("cannot create %s: %s\n", file.path, strerror(errno));
        tmpfile_close(&file);
        return 1;
    }
    const int fd = file.fd;

    enum { kBlock = 4096 };                /* multiple of the sector size */
    void *buf = NULL;                      /* address aligned to 4096 */
    if (posix_memalign(&buf, kBlock, kBlock) != 0) { tmpfile_close(&file); return 1; }
    memset(buf, 'D', kBlock);
    for (off_t i = 0; i < 16; ++i)         /* offset and length aligned too */
        if (pwrite(fd, buf, kBlock, i * 4096) != 4096) { free(buf); tmpfile_close(&file); return 1; }
    ssize_t n = pread(fd, buf, kBlock, 3 * 4096);  /* from the device */
    ssize_t bad = pread(fd, buf, kBlock, 6000);    /* offset not aligned */
    int why = errno;                       /* EINVAL on ext4 and XFS */
    free(buf);

    printf("file descriptor is %s\n", file.direct ? "direct (O_DIRECT)" : "buffered (fall back)");
    printf("16 blocks of %d bytes written with pwrite\n", kBlock);
    printf("pread of block 3       : %ld bytes\n", (long)n);
    if (bad < 0)
        printf("pread at offset 6000   : -1, errno %d (%s)\n", why, strerror(why));
    else
        printf("pread at offset 6000   : %ld bytes, so this path accepts a misaligned offset\n"
               "                         (a buffered descriptor always does; ext4 and XFS with O_DIRECT answer EINVAL)\n",
               (long)bad);

    /* Third rule: the address of the buffer. One byte past an aligned address is as misaligned as it gets. */
    static _Alignas(4096) char block[2 * 4096];
    const uintptr_t addr = (uintptr_t)(void *)block;
    printf("static buffer address  : %lu past a 4096 boundary\n", (unsigned long)(addr % 4096));
    const ssize_t odd = pread(fd, block + 1, kBlock, 0);
    const int odd_why = errno;
    if (odd < 0)
        printf("pread into address + 1 : -1, errno %d (%s)\n", odd_why, strerror(odd_why));
    else
        printf("pread into address + 1 : %ld bytes (newer kernels only ask for the DMA alignment of the\n"
               "                         device, often less than a block: do not rely on it, align to 4096)\n"
               "first byte read back   : '%c' (D is what we wrote)\n",
               (long)odd, block[1]);
    tmpfile_close(&file);
#else
    printf("this sample needs Linux: posix_memalign, pwrite and pread of 4 KiB blocks on an O_DIRECT\n"
           "file, and EINVAL for the read at offset 6000\n");
#endif
    return 0;
}
