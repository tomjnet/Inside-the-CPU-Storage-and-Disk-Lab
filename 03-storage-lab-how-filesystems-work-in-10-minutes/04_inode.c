/* How Filesystems Work: From File Name to Disk Blocks - slide 4: the inode: everything except the name (C version of 04_inode.cpp) */
/* Build: make 04_inode_c */
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

/* ---- the slide ---- */
/* An inode: everything about a file except its name. */
typedef struct { uint32_t start; uint32_t count; } Extent;  /* a run of blocks */

/* C has no std::vector: the extents sit inline in the inode, like the 4 of ext4 */
#define INLINE_EXTENTS 4
typedef struct {
    uint16_t mode;           /* file or directory, permissions */
    uint16_t links;          /* names that point here; 0 frees it */
    uint64_t size;           /* bytes */
    Extent extents[INLINE_EXTENTS];    /* where the data lives */
    size_t n_extents;
} Inode;
/* ext4: 256 byte inodes, 4 extents inline, then an extent tree. */
/* stat() reads the inode only: no data block is touched. */
/* ---- end of the slide ---- */

#define BLOCK ((uint64_t)4096)                               /* filesystem block, bytes */
#define REGULAR_FILE ((uint16_t)0100644)                     /* S_IFREG plus rw-r--r--, as stat prints it */

int main(void) {
    /* A 1 GiB file written in three runs: three extents describe all of it. */
    Inode video = {0};
    video.mode = REGULAR_FILE;
    video.links = 1;
    video.size = 1ull << 30;
    video.extents[0] = (Extent){34816, 131072};
    video.extents[1] = (Extent){190000, 98304};
    video.extents[2] = (Extent){400000, 32768};
    video.n_extents = 3;

    uint64_t covered = 0;
    printf("inode of a 1 GiB file, the model\n");
    printf("  mode %o  links %u  size %" PRIu64 " bytes\n", (unsigned)video.mode, (unsigned)video.links,
           video.size);
    for (size_t i = 0; i < video.n_extents; ++i) {
        const Extent *e = &video.extents[i];
        printf("  extent: start block %" PRIu32 ", count %" PRIu32 "\n", e->start, e->count);
        covered += (uint64_t)e->count * BLOCK;
    }
    printf("  extents cover %" PRIu64 " bytes with %zu entries\n", covered, video.n_extents);
    printf("  no name field anywhere: sizeof(Inode) here is %zu bytes of metadata\n\n", sizeof(Inode));

    /* The older design: block pointers. 4 KiB blocks, 4 byte pointers: 1024 pointers per indirect block. */
    const uint64_t per_block = BLOCK / 4;
    const uint64_t blocks = video.size / BLOCK;
    const uint64_t direct = 12;
    const uint64_t single = per_block;
    const uint64_t dbl = per_block * per_block;
    printf("the same file with ext2 style block pointers\n");
    printf("  data blocks to point at: %" PRIu64 "\n", blocks);
    printf("  12 direct pointers reach      %" PRIu64 " KiB\n", direct * BLOCK / 1024);
    printf("  one indirect block reaches    %" PRIu64 " MiB\n", (direct + single) * BLOCK / (1024 * 1024));
    printf("  a double indirect reaches     %" PRIu64 " GiB\n",
           (direct + single + dbl) * BLOCK / ((uint64_t)1024 * 1024 * 1024));
    const uint64_t left = blocks - direct - single;          /* blocks that need the double indirect level */
    const uint64_t pointer_blocks = 1 + 1 + (left + per_block - 1) / per_block;
    printf("  pointer blocks needed: %" PRIu64 " (each one an extra read), against %zu extents inside the inode\n",
           pointer_blocks, video.n_extents);

    if (covered != video.size) {
        printf("FAIL: the extents do not cover the file\n");
        return 1;
    }
    return 0;
}
