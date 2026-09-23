/* How Filesystems Work: From File Name to Disk Blocks - slide 8: lab: create a file in a tiny filesystem (C version of 08_create_file.cpp) */
/* Build: make 08_create_file_c */
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
#include <string.h>

typedef struct { uint32_t start; uint32_t count; } Extent;  /* a run of blocks */

/* C has no std::vector: each inode holds a fixed extent array and its length */
#define MAX_EXTENTS 8
typedef struct {
    uint16_t mode;           /* file or directory, permissions */
    uint16_t links;          /* names that point here; 0 frees it */
    uint64_t size;           /* bytes */
    Extent extents[MAX_EXTENTS];       /* where the data lives */
    size_t n_extents;
} Inode;

#define ROOT ((uint32_t)2)                  /* the root directory is inode 2 on ext4 */
#define FIRST_INODE ((uint32_t)11)          /* ext4 reserves inodes 1 to 10 */
#define BLOCK ((uint64_t)4096)
#define IS_DIR ((uint16_t)0040755)
#define IS_FILE ((uint16_t)0100644)

#define NINODES 32                          /* fixed when the filesystem is made */
#define NBLOCKS 64                          /* block bitmap, 64 blocks of 4 KiB */
#define MAX_DIRENTS 32
#define NAME_MAX_LEN 32

/* C has no std::map: the directories are one table of (directory inode, name, inode) rows */
typedef struct { uint32_t dir; uint32_t ino; char name[NAME_MAX_LEN]; int live; } Dirent;

/* A tiny filesystem in memory: an inode table, a directory table and a block bitmap. */
typedef struct {
    Inode inodes[NINODES];
    unsigned char used[NBLOCKS];            /* 1 = used */
    Dirent dirents[MAX_DIRENTS];
    uint32_t next_inode;                    /* the model never reuses an inode number */
} TinyFs;

static void fs_init(TinyFs *fs) {
    memset(fs, 0, sizeof *fs);
    for (uint32_t b = 0; b < 4; ++b) fs->used[b] = 1;         /* superblock, two bitmaps, inode table */
    fs->inodes[ROOT].mode = IS_DIR;
    fs->inodes[ROOT].links = 2;
    fs->next_inode = FIRST_INODE;
}
static uint32_t blocks_for(uint64_t n) { return (uint32_t)((n + BLOCK - 1) / BLOCK); }
static uint32_t free_blocks(const TinyFs *fs) {
    uint32_t n = 0;
    for (size_t b = 0; b < NBLOCKS; ++b) n += fs->used[b] ? 0u : 1u;
    return n;
}

static Dirent *dir_find(TinyFs *fs, uint32_t dir, const char *name) {
    for (size_t i = 0; i < MAX_DIRENTS; ++i) {
        Dirent *d = &fs->dirents[i];
        if (d->live && d->dir == dir && strcmp(d->name, name) == 0) return d;
    }
    return NULL;
}
static void dir_set(TinyFs *fs, uint32_t dir, const char *name, uint32_t ino) {
    Dirent *d = dir_find(fs, dir, name);
    for (size_t i = 0; d == NULL && i < MAX_DIRENTS; ++i)
        if (!fs->dirents[i].live) d = &fs->dirents[i];
    if (d == NULL) return;                                    /* table full: the model drops the name */
    d->dir = dir;
    d->ino = ino;
    d->live = 1;
    snprintf(d->name, sizeof d->name, "%s", name);
}

/* slide 6, unchanged */
static Extent fs_alloc(TinyFs *fs, uint32_t want) {
    uint32_t b = 0, n = NBLOCKS;
    while (b < n && fs->used[b]) ++b;          /* first free block */
    Extent e = {b, 0};
    for (; b < n && !fs->used[b] && e.count < want; ++b, ++e.count)
        fs->used[b] = 1;                       /* grow the run */
    return e;        /* shorter than want: the file fragments */
}

/* ---- the slide ---- */
/* create = one inode + the data blocks + one directory entry */
static uint32_t fs_create(TinyFs *fs, uint32_t dir, const char *name, uint64_t n) {
    uint32_t ino = fs->next_inode++;           /* from the inode table */
    Inode *f = &fs->inodes[ino];
    f->links = 1;
    f->size = n;
    for (uint32_t need = blocks_for(n); need > 0 && f->n_extents < MAX_EXTENTS;
         need -= f->extents[f->n_extents - 1].count)
        f->extents[f->n_extents++] = fs_alloc(fs, need);  /* from the block bitmap */
    dir_set(fs, dir, name, ino);               /* the name lives here */
    return ino;
}
/* ---- end of the slide ---- */

/* Remove a name; when the last link goes, the blocks return to the bitmap. */
static void fs_unlink(TinyFs *fs, uint32_t dir, const char *name) {
    Dirent *d = dir_find(fs, dir, name);
    if (d == NULL) return;
    const uint32_t ino = d->ino;
    d->live = 0;
    Inode *f = &fs->inodes[ino];
    if (--f->links > 0) return;
    for (size_t i = 0; i < f->n_extents; ++i)
        for (uint32_t b = f->extents[i].start; b < f->extents[i].start + f->extents[i].count; ++b)
            fs->used[b] = 0;
    memset(f, 0, sizeof *f);
}

static void bits(const TinyFs *fs, char *out, size_t cap) {
    size_t k = 0;
    for (size_t i = 0; i < NBLOCKS && k + 2 < cap; ++i) {
        if (i > 0 && i % 8 == 0) out[k++] = ' ';
        out[k++] = fs->used[i] ? '1' : '0';
    }
    out[k] = '\0';
}
static void print_bits(const TinyFs *fs) {
    char line[96];
    bits(fs, line, sizeof line);
    printf("  bitmap  %s\n", line);
}

/* The slide's create() assumes the blocks exist; a real filesystem checks first and returns ENOSPC. */
static uint32_t create_checked(TinyFs *fs, const char *name, uint64_t bytes) {
    if (free_blocks(fs) < blocks_for(bytes) || fs->next_inode >= NINODES) {
        printf("  %s: ENOSPC, no space left on device\n", name);
        return 0;
    }
    const uint32_t ino = fs_create(fs, ROOT, name, bytes);
    const Inode *f = &fs->inodes[ino];
    fs->inodes[ino].mode = IS_FILE;
    printf("  %s: inode %" PRIu32 ", %" PRIu64 " bytes, extents", name, ino, bytes);
    for (size_t i = 0; i < f->n_extents; ++i)
        printf(" [start %" PRIu32 ", count %" PRIu32 "]", f->extents[i].start, f->extents[i].count);
    printf("\n");
    return ino;
}

static int cmp_dirent_name(const void *a, const void *b) {
    return strcmp((*(Dirent *const *)a)->name, (*(Dirent *const *)b)->name);
}

int main(void) {
    /* static: the tables stay off the stack */
    static TinyFs fs;
    fs_init(&fs);
    printf("TinyFs: %d blocks of %" PRIu64 " bytes, %d inodes, blocks 0 to 3 hold the metadata\n", NBLOCKS, BLOCK,
           NINODES);
    print_bits(&fs);
    printf("\ncreate three files\n");
    create_checked(&fs, "a.log", 20000);            /* 5 blocks */
    create_checked(&fs, "b.tmp", 12000);            /* 3 blocks */
    create_checked(&fs, "c.db", 24000);             /* 6 blocks */
    print_bits(&fs);
    printf("\ndelete the middle one: a hole of 3 blocks\n");
    fs_unlink(&fs, ROOT, "b.tmp");
    print_bits(&fs);
    printf("\ncreate a file of 8 blocks: first fit fills the hole, then the end\n");
    const uint32_t d = create_checked(&fs, "d.bin", 32768);
    print_bits(&fs);
    printf("\nthe root directory, the only place that stores names\n");
    /* std::map keeps names sorted; the C table does not, so sort the root's rows before printing */
    Dirent *root_rows[MAX_DIRENTS];
    size_t n_rows = 0;
    for (size_t i = 0; i < MAX_DIRENTS; ++i)
        if (fs.dirents[i].live && fs.dirents[i].dir == ROOT) root_rows[n_rows++] = &fs.dirents[i];
    qsort(root_rows, n_rows, sizeof root_rows[0], cmp_dirent_name);
    for (size_t i = 0; i < n_rows; ++i) printf("  %s -> inode %" PRIu32 "\n", root_rows[i]->name, root_rows[i]->ino);
    printf("\na file too large for the free blocks\n");
    create_checked(&fs, "huge.iso", 64ull * BLOCK);

    const Inode *f = &fs.inodes[d];
    const int ok = d != 0 && f->n_extents == 2 && f->extents[0].start == 9 && f->extents[0].count == 3 &&
                   f->extents[1].start == 18 && f->extents[1].count == 5;
    if (!ok) {
        printf("FAIL: d.bin did not come back as the two extents the slide promises\n");
        return 1;
    }
    printf("\nd.bin is fragmented: 2 extents for one file, because the disk had a hole\n");
    return 0;
}
