/* Blocks, Sectors and Pages: How Storage Is Organized - slide 11: aligning your own i/o (C version of 11_align_io.cpp) */
/* Build: make 11_align_io_c */
#if defined(__linux__)
#define _GNU_SOURCE                  /* sched_setaffinity, CPU_SET, O_DIRECT, clock_gettime */
#endif
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS      /* fopen, strerror: MSVC deprecates the standard names */
#endif

#include <errno.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define kBlock UINT64_C(4096)        /* filesystem block, bytes */

/* round the start down and the end up to the block size;
   C has no constexpr functions, so macros keep them usable in _Static_assert */
#define ALIGN_DOWN(x, a) ((x) / (a) * (a))
#define ALIGN_UP(x, a)   (((x) + (a) - 1) / (a) * (a))

_Static_assert(ALIGN_DOWN(8292, kBlock) == 8192 && ALIGN_UP(9292, kBlock) == 12288, "align");
_Static_assert(ALIGN_DOWN(8192, kBlock) == 8192 && ALIGN_UP(8192, kBlock) == 8192, "aligned stays put");

static unsigned char pattern(uint64_t i) { return (unsigned char)(i % 251); }

int main(void) {
    /* want bytes [8292, 9292): one aligned read covers them */
    uint64_t start = ALIGN_DOWN(UINT64_C(8292), kBlock);         /* 8192 */
    uint64_t end   = ALIGN_UP(UINT64_C(8292) + 1000, kBlock);    /* 12288 */
    _Alignas(4096) static unsigned char buffer[4096];  /* aligned memory */

    printf("wanted  [8292, 9292): 1000 bytes\n");
    printf("aligned [%" PRIu64 ", %" PRIu64 "): %" PRIu64 " whole block, %" PRIu64 " bytes\n",
           start, end, (end - start) / kBlock, end - start);
    printf("buffer address modulo 4096: %" PRIu64 "\n", (uint64_t)((uintptr_t)buffer % 4096));

    /* a 16 KiB file with a known pattern, in the temporary directory */
    const char *dir = NULL;
#if defined(_WIN32)
    dir = getenv("TEMP");
#else
    dir = "/tmp";
#endif
    if (dir == NULL || dir[0] == '\0') dir = ".";
    char file[1024];
    snprintf(file, sizeof file, "%s/tomjnet_storage_lab_02_align_io_c.bin", dir);

    FILE *out = fopen(file, "wb");
    if (out == NULL) {
        printf("cannot create %s: %s\n", file, strerror(errno));
        return 1;
    }
    for (uint64_t i = 0; i < 4 * kBlock; ++i) fputc(pattern(i), out);
    fclose(out);

    /* one aligned request: seek to a block boundary, read a whole block */
    int ok = end - start == sizeof buffer;
    FILE *in = fopen(file, "rb");
    if (in == NULL) {
        printf("cannot open %s: %s\n", file, strerror(errno));
        ok = 0;
    } else {
        size_t got = 0;
        if (fseek(in, (long)start, SEEK_SET) == 0) got = fread(buffer, 1, sizeof buffer, in);
        ok = ok && got == sizeof buffer;
        fclose(in);
    }

    /* pick our 1000 bytes out of the middle of the buffer */
    const uint64_t skip = 8292 - start;
    uint64_t matching = 0;
    for (uint64_t i = 0; i < 1000; ++i)
        if (buffer[(size_t)(skip + i)] == pattern(8292 + i)) ++matching;
    printf("our bytes start %" PRIu64 " bytes into the buffer: %" PRIu64 " of 1000 match the file\n",
           skip, matching);
    ok = ok && matching == 1000;

    remove(file);
    printf("buffered I/O would have accepted [8292, 9292) as it is: the page cache rounds for you.\n"
           "O_DIRECT would not: offset, length and buffer address must be multiples of the\n"
           "logical sector size, or the call fails with EINVAL (episode 8)\n");
    return ok ? 0 : 1;
}
